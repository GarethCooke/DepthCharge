// depthcharge/binance/binance_adapter.hpp — Binance spot frames -> FeedEvents.
//
// The third adapter, and the first one written with an independent answer
// already in the room. At Kraken, B1 shipped a book that had never been checked
// and B2 supplied the CRC a week later. Here the oracle exists on day one:
// `@depth20`'s `lastUpdateId` coincides with a diff's `u` on every payload of
// every complete capture, so a book maintained by this file can be graded
// against the venue's own top-20 at ~10 opportunities a second.
//
// -------------------------------------------------------------------------
// `U`/`u` IS A TRANSPORT CHECK. IT SAYS NOTHING ABOUT WHETHER THE BOOK IS RIGHT.
// -------------------------------------------------------------------------
// This sentence is in the header because the next reader's instinct will be to
// treat a clean sequence as a clean book, and stage 0 measured exactly how wrong
// that is. Four implementations were replayed against one capture:
//
//     implementation                          oracle      U/u
//     honest control                          GREEN       GREEN
//     `qty:0` not treated as a removal        RED (884)   GREEN
//     bid and ask sides swapped               RED (884)   GREEN
//     book bounded to the rendered depth      RED (722)   GREEN
//
// **`U`/`u` scored 0 of 3**, while remaining 890/890 clean against a client that
// was 82.4% wrong. What it IS good at is the other thing entirely: in the
// deliberate-reconnect capture it caught the break on the first event after the
// socket came back — `prev_u = 99076647376`, next `U = 99076649580`, **2,204
// updates missed**. That is its whole job, it does it exactly, and it is wired
// to `Gap{SeqGap}` and to nothing else.
//
// -------------------------------------------------------------------------
// TWO TRUNCATION BOUNDARIES, WHICH IS NEW HERE
// -------------------------------------------------------------------------
// Kraken's adapter holds its book at the subscribed depth and emits at the same
// depth, so storage and emission coincide and there is one boundary. They cannot
// coincide at this venue:
//
//   * STORAGE must be deep. `binance_oracle.py --window-sweep` bounds a correct
//     client's book and grades it: on one deep-seed witness a 256-level book
//     fails 33 of 235 graded ticks while a 500-level one is clean, and on a
//     second witness taken an hour later 100 is already clean. The requirement
//     is a property of how far the market walked, not of the venue, so the only
//     size that is not a bet is one that holds the whole seed — 1,024.
//   * EMISSION cannot be deeper than the engine's book, which is
//     `kBookCapacity` == `kMaxSnapshotLevels` == 256. A Delta for a level at
//     rank 400 would be counted as `deltas_overflowed` and dropped, and the
//     engine's book would stop mirroring this one.
//
// So the adapter maintains 1,024 and emits the top `kBinanceEmitDepth`. A level
// crossing that boundary in either direction is emitted as the crossing it is: a
// level pushed out of the window becomes `Delta{qty=0}`, and a level pulled into
// it by a removal above becomes a `Delta` carrying its quantity. The engine's
// book is then an exact mirror of this one's top N, which is the property every
// golden rests on and the one a single boundary gives away for free.
//
// -------------------------------------------------------------------------
// A REMOVAL FOR A LEVEL WE DO NOT HOLD IS NORMAL, AND CONSTANT
// -------------------------------------------------------------------------
// Documented venue behaviour, and not a footnote: a book seeded at 1,000 levels
// receives removals for levels outside the seeded window continuously, because
// the venue's own book is deeper than anything it ever sends us. It is a
// counted no-op — never an error, never a `Gap`. `levels_absent_removals` is
// expected to be large and a value of zero would be the surprising reading.
//
// -------------------------------------------------------------------------
// WHAT THE ORACLE DOES NOT REACH
// -------------------------------------------------------------------------
// `@depth20` validates the top **20** levels a side while the panel draws **25**.
// That is the same shape of gap as Kraken's CRC-10-of-25 and it is smaller — 20
// of 25 against 10 of 25 — but it is not zero, and it belongs on the report line
// rather than in a comment. `validated_depth` in the harness venue table is
// still 0 because nothing in THIS build performs the comparison; C is where a
// rendered row starts claiming it was checked.
//
// -------------------------------------------------------------------------
// THE SEED IS NOT PUBLISHED UNTIL THE FEED HAS SPOKEN (M5 stage C)
// -------------------------------------------------------------------------
// DESIGN strain 26, remedy (a), chosen from four. B1 measured the defect on the
// wire: `...@depth@100mss` — one character wrong — returns HTTP 101, answers its
// pings in 0.107 ms and delivers no depth frame ever, and this adapter drew a
// populated, coloured, **LIVE** 100-level ladder over it. Invariant #5's one
// forbidden output, arriving through the mechanism installed to prevent it.
//
// The mechanism was never a missing detector. `adopt_seed()` emitted the
// `Snapshot` from the REST body, so `Book::adopt` set `FeedStatus::Live` off a
// document the *feed* had not corroborated — while `bracket_checked_` stayed
// false for ever, because no diff ever came to satisfy it. **So the fix is not
// to detect the lie but to stop asserting it: the Snapshot is emitted at the
// instant the bracket is satisfied, and not before.** Between the seed and that
// instant the ladder is held here, the engine's book is uninitialised — an
// explicit state since M4 stage C and grey since M4 stage D — and the panel is
// grey with no threshold, no new detector, no new `GapReason` and no new
// `FeedEvent::Kind`. §6 and §4 do not move.
//
// WHY (a) AND NOT (b), (c) OR (d), one line each, because the other three were
// costed rather than dismissed:
//
//   (b) the ping does not stamp liveness until the bracket is satisfied — it
//       does not work on the board, and that is a finding rather than a
//       preference: `LivenessWatchdog::expired()` is gated on `armed_`, which
//       only the first `on_liveness` sets, so withholding liveness on a lying
//       socket leaves the watchdog UNARMED and the lie never self-terminates.
//       It would need a never-armed-since-connect deadline, which is (c).
//   (c) a never-started detector — a genuinely new question with a threshold of
//       its own to calibrate, and it needs firmware this stage may not touch.
//   (d) grade the seeded book against `@depth20` — correct, and it requires the
//       audit stream to be subscribed, which is a board decision (D's). Nothing
//       here forecloses it; it would arrive as a second, stronger witness.
//
// WHAT IT COSTS, STATED. A fraction of a second of grey on a healthy connect
// (the seed-to-first-diff gap, ~100 ms on BTCUSDT at the 100 ms tick), and up to
// ~10.5 s on a quiet pair, which is this venue's measured worst legitimate
// inter-message gap (M5 stage 0). No committed capture loses a Snapshot to it.
//
// **AND WHAT IT DOES NOT BUY, WHICH IS THE HALF THAT MATTERS.** It closes the
// window at CONNECT and nothing else. The ping is emitted by the WebSocket layer
// BELOW the subscription, so it proves the socket and never the feed — and a
// subscription dropped server-side an hour into a session still presents exactly
// as health, to this remedy and to the liveness clock alike. What (a) converts is
// *permanent* into *bounded*, and bounded is where invariant #5 draws its line:
// a panel that greys late is honest, a panel that never greys is the one
// unacceptable output. ARCHITECTURE §9, 2026-08-25 (both M5 stage B1 rows).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "depthcharge/binance/binance_frame.hpp"
#include "depthcharge/feed_event.hpp"
#include "depthcharge/ladder.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge::binance {

// How deep the adapter EMITS. Bounded by the engine's book, which is where a
// deeper Delta would be dropped rather than applied.
inline constexpr std::uint32_t kBinanceEmitDepth =
    static_cast<std::uint32_t>(kMaxSnapshotLevels);
static_assert(kBinanceEmitDepth <= kBinanceMaxFrameLevels,
              "the emitted window cannot be deeper than the book it is a window onto");

// THE PRE-SEED BUFFER, sized from measurement.
//
// The venue's documented procedure buffers diffs while the REST fetch is in
// flight, then drops the ones the snapshot already contains. That buffering is
// not optional and it is not a nicety: a /api/v3/depth round trip measures
// ~1.0-1.5 s, so the snapshot names an instant the stream has already moved
// 10-15 events past. Drop those events and the book is missing 1.4 s of
// amendments; drop them and re-fetch instead and the next snapshot is stale by
// the same margin, for ever.
//
// Measured over the seven committed slices: worst case **15 events carrying 823
// levels (12.9 KiB)**, of which at most 5 events / 537 levels survive the
// snapshot. The bounds below are ~2.5x that, and an overflow is NOT silent — it
// raises `Gap{Overflow}` and asks for a re-seed, which is the honest answer for
// a connect that took longer than the buffer was sized for.
inline constexpr std::uint32_t kBinanceBufferEvents = 64;
inline constexpr std::uint32_t kBinanceBufferLevels = 2048;

// ===========================================================================
// THE RE-SNAPSHOT TRIGGER (M5 stage B2), AND WHY IT IS NOT B1'S LOW-WATER MARK
// ===========================================================================
//
// B1 built an instrument and described it as *the seeded-window edge*: when the
// market walks far enough that the rendered window approaches the edge of what
// the seed contained, `min_bid_levels` / `min_ask_levels` is where it shows. The
// description is the right quantity. **The counter is not that quantity**, and
// B2 measured the gap rather than inheriting the sentence:
//
//     capture (limit=100)                low-water HELD   low-water COVERED
//     binance_btcusdt_d1000ms_20260824      100 / 100          0 / 103
//     binance_btcusdt_reconnect_20260824    100 / 100          0 / 100
//
// Both of those are the 82.4% failure class in progress -- the bid side walks
// clean out of the seeded range -- and `min_bid_levels` reads a flat, healthy
// **100** through all of it. It cannot fall: the count of levels HELD only ever
// grows, because every diff that removes a level near the touch is accompanied
// by others adding prices the seed never contained. A trigger on it would be a
// trigger that never fires.
//
// WHAT ACTUALLY ERODES IS COVERAGE, AND IT IS A DIFFERENT NUMBER.
// A `/api/v3/depth` body gives a complete picture of one price RANGE: from the
// touch down to its worst bid, and up to its worst ask. Inside that range the
// book is complete for ever after, because every subsequent change arrives as a
// diff. **Outside it the client is permanently ignorant** -- a resting order
// that predates the seed and is never restated never enters the diff stream, so
// it is invisible until the next snapshot. So:
//
//     seeded coverage (bids) = held bids at px >= the seed's worst bid
//     seeded coverage (asks) = held asks at px <= the seed's worst ask
//
// Those are the levels this client can vouch for. As the touch walks toward a
// seeded boundary the levels between it and the boundary are consumed, coverage
// falls, and when it falls below `kBinanceEmitDepth` the ladder is drawing rows
// from the region the client was never told about. THAT is the observable, and
// unlike a wall clock it is self-scaling by construction: a fast market
// exhausts the window sooner and asks sooner, with no constant claiming to know
// how fast the market is.
//
// ---------------------------------------------------------------------------
// SIZING THE MARGIN -- AGAINST A BOUNDED FETCH, NOT AGAINST A PERCENTILE
// ---------------------------------------------------------------------------
//
// The book is UNBRACKETED for the whole of a fetch, so the trigger has to fire
// while enough coverage remains to survive one. The obvious sizing is
// `margin >= walk rate x p99 fetch latency`, and the corpus cannot support the
// second term: 27 committed REST records, two `limit` tiers, and the 99th
// percentile of 27 samples IS the maximum.
//
// **It also does not need to, because the distribution has no tail worth sizing
// against on this path.** Measured over every committed REST record, both tiers:
//
//     tier         n    median      max      max/median
//     limit=100   22   1,009.4 ms  1,063.0 ms   1.05
//     limit=1000  13   1,503.1 ms  1,590.7 ms   1.06
//
// Choosing max over median moves the margin by 87.6 ms -- $0.029 of BTCUSDT walk
// against a $224.52 seeded window, 0.013% of it. A percentile is the wrong
// instrument for a quantity that flat.
//
// **AND EVERY ONE OF THOSE 27 IS A DESK-BOX MEASUREMENT.** Wired ethernet,
// CPython, urllib. The board is an ESP32-S3 on Wi-Fi doing TLS, and M4 stage B3
// measured DNS failures on that platform at a flat 14,000 ms. A desk figure must
// never stand as the board's, so the margin is sized against a DEADLINE THE
// TRANSPORT IMPOSES rather than against a latency anybody measured:
//
//     margin >= walk rate x T,  T = kBinanceFetchDeadlineMs
//
// A fetch that exceeds T is abandoned and retried. That covers 100% of fetches
// by construction instead of 99% of a sample of 27, and it converts an unbounded
// unbracketed window into a bounded one -- which is where invariant #5 draws its
// line, and the same move B1's remedies (a) and (b) both make.
//
// **T IS A REQUIRED TRANSPORT PROPERTY AND THIS FILE CANNOT ENFORCE IT.** The
// adapter has no clock and issues no fetch; it latches `reseed_wanted()` and the
// layer that can, does. Recorded here because the constant below is sized
// against T, so a transport that does not impose one silently invalidates the
// margin. ARCHITECTURE.md 9 carries it as a decision. Implementing it is C/D's.
//
// WHY 15 s. `tools/capture_binance.py` already runs this exact fetch at a 15 s
// cadence behind `REST_DRAIN_TIMEOUT_S = 20`, so it is a value with an
// implementation behind it rather than a fresh guess -- and it survives the
// sanity check against the window it is protecting: at BTCUSDT's measured
// $0.33/s walk, a `limit=1000` seed's $224.52 is ~677 s of walking and 15 s is
// **2.2%** of it, while `limit=100`'s $15.63 is ~47 s and 15 s is **32%**. That
// is an independent argument for `limit=1000` arriving from the schedule rather
// than from the depth sweep.
inline constexpr std::uint32_t kBinanceFetchDeadlineMs = 15000;

// WHAT THE DEADLINE COSTS IN LEVELS. The worst loss of seeded coverage over any
// window of `kBinanceFetchDeadlineMs` or less, measured across all nine
// BTCUSDT and ATOMEUR captures in the corpus, is **168 levels** (mixed1). The
// loss is burst-dominated rather than smooth -- the single worst event is one
// 100 ms tick in `deepseed` that took the best bid down $15.99 and removed
// **135** covered levels at once -- so a mean rate would understate it by an
// order of magnitude and a max is the honest term.
//
// 192 is 168 plus 14%, and the 14% is not decoration: 168 is a maximum over nine
// captures on two pairs on two days, which is a sample and not a distribution,
// and this is the same market whose depth requirement moved 5x between two
// witnesses an hour apart.
inline constexpr std::uint32_t kBinanceReseedMarginLevels = 192;

// THE TRIGGER. Below this many covered levels on either side, the seed is
// exhausting and the adapter asks for another.
//
// The floor is `kBinanceEmitDepth` rather than the panel's 25 rows because the
// emitted window is what the ENGINE holds, and a window policy chooses among
// those 256 -- so a level that is wrong at rank 200 can be drawn. Sizing to the
// rendered depth would be sizing to today's window policy.
inline constexpr std::uint32_t kBinanceReseedCoverLevels =
    kBinanceEmitDepth + kBinanceReseedMarginLevels;
static_assert(kBinanceReseedCoverLevels <
                  static_cast<std::uint32_t>(kBinanceRestLimit),
              "the trigger must sit below the seed depth, or every seed fires it "
              "on arrival and the client re-fetches for ever");

// ---------------------------------------------------------------------------
// THE DECLARED SYMBOLS, AND WHY THE SCALE IS NOT THE TICK SIZE
// ---------------------------------------------------------------------------
//
// §4's declare-and-verify rule reads differently here from either predecessor.
// Anvil publishes no tick metadata so DepthCharge must declare it; Kraken
// publishes precision per symbol and the declarations were measured against it.
// **Binance publishes a per-symbol `tickSize` that is NOT the wire precision**,
// and following it would be wrong by six orders of magnitude.
//
// Every price and quantity this venue sends carries EXACTLY 8 decimals,
// whatever the instrument: 202,012 of 202,012 entries at stage 0, re-measured at
// B1 as 188,372 of 188,372 across the committed slices, on a 2 dp symbol and a
// 3 dp symbol alike. `78764.46000000` and `1.31100000` are both 8 dp. So
// `price_decimals` is a venue constant of 8 and has nothing to do with the
// instrument; deriving it from BTCUSDT's `tickSize` of `0.01` would scale by
// 10^2 where the wire scales by 10^8.
//
// The filters are still worth having — as VALIDATORS. `tick_ticks` and
// `step_ticks` are `PRICE_FILTER.tickSize` and `LOT_SIZE.stepSize` expressed at
// the 8-decimal scale, and the parser checks that every value is a whole
// multiple. **They were verified before being declared**, because a validator
// that rejects valid frames is worse than none: the GCD of every price and every
// quantity in the committed corpus is exactly the declared filter, over 91,665
// BTCUSDT entries and 2,521 ATOMEUR ones.
//
//     symbol     price GCD                = tick        qty GCD           = step
//     BTCUSDT    1,000,000                0.01          1,000             0.00001
//     ATOMEUR      100,000                0.001         1,000,000         0.01
//
// **AND THIS IS THE PRECISION DETECTOR the brief asks to be named.** The
// equivalence "wire precision == 8 decimals" is an assumption, and the first
// thing that notices if it stops being true is `ParseStatus::BadPrice` from
// `parse_scaled` — a named status with a message, raised on the value that
// disagreed, never a book that silently drifts. A ninth decimal would fail
// `TooManyDecimals`; a coarser tick would fail the modulo above.
//
// `id` is a DepthCharge-side integer, not a venue id: §4 keeps the engine
// integer-only, so the string symbol stops at the adapter.
inline constexpr SymbolConfig kBinanceBtcUsdt{
    SymbolSpec{/*id=*/11, /*price_decimals=*/8, /*qty_decimals=*/8}, "BTCUSDT",
    /*tick_ticks=*/1'000'000, /*step_ticks=*/1'000};
inline constexpr SymbolConfig kBinanceAtomEur{
    SymbolSpec{/*id=*/12, /*price_decimals=*/8, /*qty_decimals=*/8}, "ATOMEUR",
    /*tick_ticks=*/100'000, /*step_ticks=*/1'000'000};
static_assert(kBinanceBtcUsdt.spec.valid(), "a declared SymbolSpec must be decodable");
static_assert(kBinanceAtomEur.spec.valid(), "a declared SymbolSpec must be decodable");

// Look a wire symbol up in the declared set. Returns false for a symbol this
// build has no scale for, which must be a loud refusal rather than a guessed
// scale: a wrong scale does not fail, it draws a wrong ladder.
constexpr bool symbol_config_for(std::string_view wire_symbol, SymbolConfig& out) noexcept {
    if (wire_symbol == kBinanceBtcUsdt.wire_symbol) { out = kBinanceBtcUsdt; return true; }
    if (wire_symbol == kBinanceAtomEur.wire_symbol) { out = kBinanceAtomEur; return true; }
    return false;
}

class BinanceAdapter {
public:
    enum class SeedState : std::uint8_t {
        Unseeded,  // buffering; no book, and updates are held not dropped
        Seeded,    // a REST body has baselined the ladder
    };

    struct Stats {
        std::uint64_t frames_in = 0;
        std::uint64_t events_out = 0;
        std::uint64_t diff_frames = 0;
        std::uint64_t partial_frames = 0;   // @depth20 — the oracle's stream
        std::uint64_t rest_snapshots = 0;   // /api/v3/depth bodies that arrived
        std::uint64_t rest_no_body = 0;     // ...and the fetches that returned none
        std::uint64_t acks = 0;
        std::uint64_t unknown_kind = 0;
        std::uint64_t parse_errors = 0;
        std::uint64_t price_errors = 0;
        std::uint64_t qty_errors = 0;
        std::uint64_t other_symbol = 0;
        std::uint64_t overflow_frames = 0;  // more levels than staging holds
        std::uint64_t transport_gaps = 0;

        // ---- the seed ------------------------------------------------------
        std::uint64_t buffered_events = 0;        // held while awaiting the seed
        std::uint64_t buffered_dropped_by_seed = 0;  // u <= lastUpdateId
        std::uint64_t buffer_overflows = 0;       // the connect outran the buffer
        std::uint64_t seed_bracket_ok = 0;        // U <= L+1 <= u on the first survivor
        std::uint64_t seed_bracket_failed = 0;    // ...and it did not
        std::uint64_t reseeds_requested = 0;

        // A SEEDED LADDER THAT THE FEED NEVER CONFIRMED, dropped without ever
        // having been published (M5 stage C). Zero on every healthy connect and
        // on all nine committed captures; it is the counter that says the
        // deferred-Snapshot remedy actually held something back, rather than the
        // absence of one saying nothing. See THE SEED IS NOT PUBLISHED UNTIL THE
        // FEED HAS SPOKEN, above.
        std::uint64_t seeds_unconfirmed = 0;

        // ---- the diff stream ----------------------------------------------
        std::uint64_t seq_breaks = 0;             // U != prev_u + 1 -> Gap{SeqGap}
        std::uint64_t levels_applied = 0;
        std::uint64_t levels_removed = 0;         // qty 0 for a level we held
        // A REMOVAL FOR A LEVEL WE DO NOT HOLD. Documented venue behaviour and
        // constant at a seeded depth of 1,000; a counted no-op, never an error.
        std::uint64_t levels_absent_removals = 0;
        std::uint64_t levels_unchanged = 0;       // re-sent at the quantity we hold
        std::uint64_t levels_evicted = 0;         // pushed out of STORAGE (1,024)
        std::uint64_t levels_outside_emit = 0;    // applied, deeper than the window
        std::uint64_t window_exits = 0;           // emitted as Delta{qty=0}
        std::uint64_t window_entries = 0;         // pulled into the window
        std::uint64_t deltas_before_seed = 0;     // arrived with no book AND no buffer

        // ---- the seeded-window edge -----------------------------------------
        // B1's instrument, KEPT AND DEMOTED. It counts levels HELD, which only
        // ever grows, so it reads a flat 100 through the very failure it was
        // described as showing. Retained because it is what says the storage
        // ladder is not being starved, and because deleting a counter whose
        // reading turned out to mean something else is how the next reader
        // repeats the mistake. Not the trigger. See `min_*_cover` below.
        std::uint32_t min_bid_levels = 0xFFFFFFFFu;
        std::uint32_t min_ask_levels = 0xFFFFFFFFu;

        // ---- SEEDED COVERAGE: the trigger's observable (M5 stage B2) --------
        // How few levels this client could still vouch for, per side, at the
        // worst moment of the run. Falls as the touch walks toward a seeded
        // boundary; reaching zero means the whole emitted window is outside
        // anything the seed described.
        std::uint32_t min_bid_cover = 0xFFFFFFFFu;
        std::uint32_t min_ask_cover = 0xFFFFFFFFu;

        // Trigger crossings: coverage fell below `kBinanceReseedCoverLevels`
        // and a re-seed was asked for. Once per seed epoch, not once per frame.
        std::uint64_t cover_triggers = 0;

        // **THE SEED ARRIVED ALREADY BELOW ITS OWN MARGIN**, so the trigger was
        // never armed for that epoch. Two causes and the adapter cannot tell
        // them apart, because it does not know what `limit` was asked for:
        // the request was too shallow (BTCUSDT at limit=100 -- a configuration
        // fault), or the venue's whole book is shallower than the margin
        // (ATOMEUR, which has 16 bids in total -- nothing is wrong at all).
        // **It does not need to tell them apart, because the response is the
        // same:** re-fetching at the same depth changes nothing, so asking again
        // would spend IP weight to receive the identical shortfall. Reported,
        // never looped on. Whoever holds the request can tell which it is.
        std::uint64_t seeds_below_margin = 0;

        // A REST body that arrived on a live book. Counted rather than silently
        // dropped -- see `on_rest_body`. `adoptable` is the subset for which
        // adopting would have lost nothing (`lastUpdateId >= last_u_`), which is
        // the measurement D needs to choose a re-seed mechanism.
        std::uint64_t resnapshots_declined = 0;
        std::uint64_t resnapshots_adoptable = 0;
    };

    explicit BinanceAdapter(const SymbolConfig& cfg) noexcept : cfg_(cfg) {}

    const SymbolSpec& symbol() const noexcept { return cfg_.spec; }
    std::string_view wire_symbol() const noexcept { return cfg_.wire_symbol; }
    const Stats& stats() const noexcept { return stats_; }
    Seq next_seq() const noexcept { return next_seq_; }
    SeedState seed_state() const noexcept { return seed_; }
    // A REST body has baselined THIS adapter's ladder. Since M5 stage C that is
    // NOT the same question as "does the engine have a book": between the seed
    // and the bracket this returns true while nothing has been published, which
    // is the whole of the remedy. `seed_confirmed()` is the second question.
    bool has_baseline() const noexcept { return seed_ == SeedState::Seeded; }

    // The feed corroborated the seed — a diff bracketed `lastUpdateId + 1` — so
    // the Snapshot went out and the engine's book exists. False on a socket that
    // holds open, answers pings and never delivers a depth frame, which is
    // precisely the case DESIGN strain 26 was opened for.
    bool seed_confirmed() const noexcept { return bracket_checked_; }
    std::int64_t last_update_id() const noexcept { return last_u_; }
    ParseStatus last_status() const noexcept { return last_status_; }
    std::uint32_t bid_count() const noexcept { return bid_count_; }
    std::uint32_t ask_count() const noexcept { return ask_count_; }

    // ---- seeded coverage, live (M5 stage B2) --------------------------------
    //
    // The count of held levels inside the price range the seed described. A
    // linear scan of the ladder per call, and it is called once per FRAME (from
    // `note_depth`) rather than once per level -- <=1,024 integer comparisons at
    // <=10 frames a second, which is microseconds on an LX7. It uses
    // `ladder::rank_of` rather than a private loop because that header exists
    // precisely so that "which of two prices ranks better" has one definition
    // (M4 stage B1); a reverse scan here would be a second copy of `better()`.
    // Zero when the bounds are unknown, ASSERTED HERE rather than inherited.
    // It is already true without the guard, because `drop_book` zeroes the
    // counts and a scan of an empty side returns 0 -- but that makes a reading
    // here correct only because of an invariant two functions away, and a later
    // change that leaves the levels in place would silently start counting them
    // against a dead seed's floor.
    std::uint32_t bid_cover() const noexcept {
        return have_seed_bounds_ ? cover(bids_, bid_count_, seed_bid_floor_, Side::Bid) : 0;
    }
    std::uint32_t ask_cover() const noexcept {
        return have_seed_bounds_ ? cover(asks_, ask_count_, seed_ask_ceil_, Side::Ask) : 0;
    }
    // Are the seeded bounds known at all? False before the first seed and after
    // any `drop_book`, when there is no range to be inside.
    bool cover_known() const noexcept { return have_seed_bounds_; }
    // Is the TRIGGER live? False additionally when the seed arrived below its
    // own margin, where asking again would buy nothing (see `seeds_below_margin`).
    bool cover_trigger_armed() const noexcept { return cover_trigger_armed_; }
    const BookLevel* bids() const noexcept { return bids_; }
    const BookLevel* asks() const noexcept { return asks_; }

    // Latched: the seed is stale or missing and the transport should fetch
    // again. Same shape as Kraken's `resync_wanted()` and for the same reason —
    // the adapter cannot fetch, so it says so and the layer that can, does.
    bool reseed_wanted() const noexcept { return reseed_wanted_; }
    void clear_reseed_wanted() noexcept { reseed_wanted_ = false; }

    // ---- one WebSocket text frame, verbatim --------------------------------
    template <typename Sink>
    void on_frame(std::string_view json, Sink&& sink) {
        ++stats_.frames_in;
        const ParseStatus st =
            parse_binance_frame(json, FrameSource::WsFrame, cfg_, frame_);
        last_status_ = st;
        if (st != ParseStatus::Ok) { return note_parse_failure(st, sink); }

        switch (frame_.kind) {
            case FrameKind::DepthUpdate:
                ++stats_.diff_frames;
                on_diff(sink);
                break;
            case FrameKind::PartialDepth:
                // THE AUDIT STREAM, AND THE ADAPTER DOES NOT APPLY IT.
                //
                // A `@depth20` payload replaces the top 20 and re-baselines
                // nothing below it, so adopting one would leave the levels
                // outside it at whatever they had — a book that is right at the
                // touch and silently wrong underneath, which is worse than a
                // book that is honestly stale. It exists to be COMPARED
                // against, and the comparison is the harness's (dc_binance_oracle)
                // and C's, not this file's.
                ++stats_.partial_frames;
                break;
            case FrameKind::Ack:     ++stats_.acks; break;
            case FrameKind::Unknown: ++stats_.unknown_kind; break;
            case FrameKind::RestSnapshot:
                // Unreachable: a WS frame is never parsed as a REST body.
                ++stats_.unknown_kind;
                break;
        }
    }

    // ---- a /api/v3/depth response body -------------------------------------
    //
    // The seed if there is none, a re-snapshot otherwise. Both go through the
    // same parse, because they are the same bytes; what differs is what the
    // adapter does with them, and that is a state question rather than a wire
    // one.
    template <typename Sink>
    void on_rest_body(std::string_view json, Sink&& sink) {
        ++stats_.frames_in;
        const ParseStatus st =
            parse_binance_frame(json, FrameSource::RestBody, cfg_, frame_);
        last_status_ = st;
        if (st != ParseStatus::Ok) { return note_parse_failure(st, sink); }
        if (frame_.kind != FrameKind::RestSnapshot || !frame_.has_last_update_id) {
            ++stats_.parse_errors;
            return;
        }
        ++stats_.rest_snapshots;
        if (seed_ == SeedState::Unseeded) { return adopt_seed(sink); }

        // A RE-SNAPSHOT ONTO A LIVE BOOK IS STILL NOT ADOPTED, AND B2 COUNTS
        // WHAT B1 DROPPED IN SILENCE.
        //
        // It is a statement about a PAST instant — the round trip is ~1.0-1.5 s,
        // so the stream has moved 10-15 events past the id it carries — and
        // adopting it wholesale would rewind the book by that much. Rolling it
        // forward instead needs the diffs covering `[lastUpdateId + 1, last_u_]`,
        // and this adapter applied and discarded those; buffering them for the
        // whole of a 15 s fetch deadline is ~150 events / 8,200 levels, about
        // **128 KiB**, against the pre-seed buffer's measured 15 / 823. So the
        // three candidate mechanisms are a 128 KiB buffer, a `Gap` and a grey
        // flash, or a live-book merge — and choosing costs board memory (D's)
        // and a rendered state (C's). **The board's re-seed behaviour is D**, so
        // this stage supplies the measurement instead of the mechanism.
        //
        // `adoptable` is that measurement: adopting is LOSSLESS whenever the
        // body is not older than the book, because there are then no events
        // between them to lose. Never true on a busy pair at a 100 ms cadence;
        // routinely true on a pair that goes 10 s between diffs. D needs to know
        // which world it is in before it pays 128 KiB for a buffer.
        ++stats_.resnapshots_declined;
        if (have_last_u_ && frame_.last_update_id >= last_u_) {
            ++stats_.resnapshots_adoptable;
        }
    }

    // ---- a REST fetch that produced no body --------------------------------
    //
    // `rest:no-body`, held as A STATE AND NOT AS A FAILURE (M5 stage A found
    // that the capture tool RECORDS a failed fetch rather than dropping it,
    // because "the snapshot did not arrive" is a fact about the capture window).
    // The adapter's answer is the same shape as Kraken's *absence of a subscribe
    // is not failure of a subscribe*: the seed has not arrived YET. Buffering
    // continues, the book stays uninitialised, nothing is dropped, and no Gap is
    // raised — there is nothing to be stale about, because there was never a
    // book. It does latch a re-seed request, because the fetch that would have
    // supplied one did not.
    void on_rest_missing() noexcept {
        ++stats_.frames_in;
        ++stats_.rest_no_body;
        if (seed_ == SeedState::Unseeded) {
            reseed_wanted_ = true;
            ++stats_.reseeds_requested;
        }
    }

    template <typename Sink>
    void on_transport_gap(GapReason reason, Sink&& sink) {
        ++stats_.transport_gaps;
        // The ladder dies with the socket, for the reason Kraken's does: a
        // reconnect is served a fresh seed, and deltas applied across a hole
        // onto a pre-hole ladder are the same "different book that looks
        // plausible" as deltas applied before any baseline.
        drop_book(reason, sink);
    }

private:
    // ---- emission ----------------------------------------------------------

    Seq take_seq() noexcept { return next_seq_++; }

    template <typename Sink>
    void emit(FeedEvent& ev, Sink& sink) {
        ev.seq = take_seq();
        ++stats_.events_out;
        sink(ev);
    }

    template <typename Sink>
    void emit_delta(PriceTicks px, Qty qty, Side side, Sink& sink) {
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Delta;
        ev.px = px;
        ev.qty = qty;
        ev.side = side;
        emit(ev, sink);
    }

    template <typename Sink>
    void emit_gap(GapReason reason, Sink& sink) {
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Gap;
        ev.reason = reason;
        emit(ev, sink);
    }

    template <typename Sink>
    void note_parse_failure(ParseStatus st, Sink& sink) {
        switch (st) {
            case ParseStatus::BadPrice:    ++stats_.price_errors; break;
            case ParseStatus::BadQty:      ++stats_.qty_errors; break;
            case ParseStatus::OtherSymbol: ++stats_.other_symbol; break;
            case ParseStatus::TooManyLevels:
                // A message this build could not stage whole. §4 already has the
                // word for it: a venue reassembly buffer overflowed, and the
                // book is unknown until the next Snapshot. No new GapReason.
                ++stats_.overflow_frames;
                drop_book(GapReason::Overflow, sink);
                break;
            default: ++stats_.parse_errors; break;
        }
    }

    template <typename Sink>
    void drop_book(GapReason reason, Sink& sink) {
        const bool had_book = seed_ == SeedState::Seeded;
        // A LADDER THIS ADAPTER HELD AND NEVER PUBLISHED (M5 stage C). It is the
        // deferred-Snapshot remedy's own evidence: a non-zero reading is a seed
        // the feed never corroborated, which before this stage would have been a
        // coloured LIVE ladder instead of a counter.
        if (had_book && !bracket_checked_) { ++stats_.seeds_unconfirmed; }
        bid_count_ = 0;
        ask_count_ = 0;
        seed_ = SeedState::Unseeded;
        last_u_ = 0;
        have_last_u_ = false;
        buf_events_ = 0;
        buf_levels_ = 0;
        bracket_checked_ = false;
        // The bounds die with the book. A coverage count against the previous
        // seed's floor, taken over levels the next seed will replace, is a
        // number about nothing.
        have_seed_bounds_ = false;
        cover_trigger_armed_ = false;
        cover_trigger_latched_ = false;
        reseed_wanted_ = true;
        ++stats_.reseeds_requested;
        // A Gap is raised whether or not there was a book: the engine's book may
        // hold levels this adapter has just discarded, and leaving it Live over
        // them is the one unacceptable output (invariant #5). `had_book` only
        // decides whether anything was actually lost, which the counter says.
        (void)had_book;
        emit_gap(reason, sink);
    }

    // ---- the seed ----------------------------------------------------------

    template <typename Sink>
    void adopt_seed(Sink& sink) {
        bid_count_ = 0;
        ask_count_ = 0;
        for (std::uint32_t i = 0; i < frame_.bid_count; ++i) {
            seed_level(bids_, bid_count_, frame_.bids[i], Side::Bid);
        }
        for (std::uint32_t i = 0; i < frame_.ask_count; ++i) {
            seed_level(asks_, ask_count_, frame_.asks[i], Side::Ask);
        }
        last_u_ = frame_.last_update_id;
        have_last_u_ = true;
        seed_ = SeedState::Seeded;
        reseed_wanted_ = false;

        // THE SEEDED BOUNDS, LATCHED HERE AND NOWHERE ELSE. The worst price on
        // each side of the body IS the edge of what this client will ever know
        // completely: inside the range every later change arrives as a diff,
        // outside it a resting order that predates the seed and is never
        // restated is invisible until the next snapshot. Everything the trigger
        // does is a count against these two numbers.
        have_seed_bounds_ = bid_count_ > 0 && ask_count_ > 0;
        seed_bid_floor_ = bid_count_ > 0 ? bids_[bid_count_ - 1].px : 0;
        seed_ask_ceil_ = ask_count_ > 0 ? asks_[ask_count_ - 1].px : 0;

        // ARM THE TRIGGER ONLY IF THE SEED CAN SATISFY ITS OWN MARGIN. If it
        // cannot, asking again would return a body of the same depth with the
        // same shortfall, at 50 IP weight a time — the "too eager" direction,
        // and the venue bans on breach. The shortfall is reported instead, and
        // it is the sizing result stated as a state: **the trigger cannot rescue
        // a seed that never satisfied its own margin.** On BTCUSDT at limit=100
        // that fires on arrival, ~60 s before the book actually goes wrong,
        // which is the 82.4% failure class caught at the seed rather than at the
        // ladder.
        const std::uint32_t worst_seed_cover =
            bid_count_ < ask_count_ ? bid_count_ : ask_count_;
        cover_trigger_armed_ =
            have_seed_bounds_ && worst_seed_cover >= kBinanceReseedCoverLevels;
        if (have_seed_bounds_ && !cover_trigger_armed_) { ++stats_.seeds_below_margin; }
        cover_trigger_latched_ = false;
        note_depth();

        // **NOTHING IS EMITTED HERE (M5 stage C).** The Snapshot used to go out
        // on this line, off the REST body alone, and that is the whole of DESIGN
        // strain 26's mechanism — see THE SEED IS NOT PUBLISHED UNTIL THE FEED
        // HAS SPOKEN at the head of this file. The ladder is now held until a
        // diff brackets it, and `replay_buffer` or `check_continuity` publishes
        // it at that instant. If neither ever does, nothing is ever published,
        // and a book nobody published cannot be drawn live.
        replay_buffer(sink);
    }

    // The seed, published at the moment the FEED corroborated it and never
    // before. Called from exactly the two places the bracket can be satisfied,
    // and from nowhere else — a third caller would be the defect coming back.
    //
    // The Snapshot conveys the EMITTED window, not the stored book: the spans
    // point into this adapter's ladder, which outlives the sink call, and are
    // clamped to what the engine can hold. The ladder is still the seed's at
    // this point — in both callers the bracketing event's own levels are applied
    // AFTER this returns, so the engine receives baseline-then-amendment in the
    // order it did before, with the same seq numbers.
    template <typename Sink>
    void publish_seed(Sink& sink) {
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Snapshot;
        ev.bids = LevelSpan{bids_, emit_count(bid_count_)};
        ev.asks = LevelSpan{asks_, emit_count(ask_count_)};
        emit(ev, sink);
    }

    // THE DOCUMENTED PROCEDURE, once, where it can be read: drop every buffered
    // event the snapshot already contains, then require the first survivor to
    // bracket L + 1.
    template <typename Sink>
    void replay_buffer(Sink& sink) {
        const std::uint32_t held = buf_events_;
        std::uint32_t first_survivor = held;
        for (std::uint32_t i = 0; i < held; ++i) {
            if (buf_[i].final_id > last_u_) { first_survivor = i; break; }
            ++stats_.buffered_dropped_by_seed;
        }

        if (first_survivor < held) {
            const BufferedEvent& ev = buf_[first_survivor];
            // U <= L + 1 <= u. If the snapshot is OLDER than the first surviving
            // event's U there is a hole between them, and the answer is to fetch
            // again rather than to proceed onto a book with a gap in its
            // provenance.
            if (ev.first_id <= last_u_ + 1 && last_u_ + 1 <= ev.final_id) {
                ++stats_.seed_bracket_ok;
                bracket_checked_ = true;
                // THE FEED HAS SPOKEN, so the seed may be published — before the
                // survivors below are applied on top of it, which is the order
                // the engine has always seen.
                publish_seed(sink);
            } else {
                ++stats_.seed_bracket_failed;
                buf_events_ = 0;
                buf_levels_ = 0;
                drop_book(GapReason::SeqGap, sink);
                return;
            }
        }

        for (std::uint32_t i = first_survivor; i < held; ++i) {
            apply_buffered(buf_[i], sink);
        }
        buf_events_ = 0;
        buf_levels_ = 0;
    }

    // ---- the diff stream ---------------------------------------------------

    template <typename Sink>
    void on_diff(Sink& sink) {
        if (seed_ == SeedState::Unseeded) { return buffer_diff(sink); }
        check_continuity(sink);
        if (seed_ == SeedState::Unseeded) { return; }  // the break dropped the book
        apply_levels(frame_.bids, frame_.bid_count, Side::Bid, sink);
        apply_levels(frame_.asks, frame_.ask_count, Side::Ask, sink);
        last_u_ = frame_.final_update_id;
        have_last_u_ = true;
        note_depth();
    }

    template <typename Sink>
    void check_continuity(Sink& sink) {
        if (!have_last_u_) { return; }
        if (!bracket_checked_) {
            // The first event after a seed with no surviving buffered event.
            // It plays the survivor's role: the venue's rule is about the first
            // event applied on top of the snapshot, not about where it came from.
            //
            // **`bracket_checked_` IS SET IN THE SUCCESS BRANCH AND NOT BEFORE
            // THE TEST (moved at M5 stage C).** It used to be set here, which was
            // harmless while nothing read it on the way out — `drop_book` clears
            // it anyway. It stopped being harmless the moment `drop_book` began
            // counting `seeds_unconfirmed` off it: a bracket FAILURE on this path
            // would have arrived at `drop_book` with the flag already true and
            // gone uncounted, while the identical failure through
            // `replay_buffer` was counted. One event, two answers, depending on
            // which of the two bracket sites saw it.
            if (frame_.first_update_id <= last_u_ + 1 &&
                last_u_ + 1 <= frame_.final_update_id) {
                bracket_checked_ = true;
                ++stats_.seed_bracket_ok;
                // THE FEED HAS SPOKEN. The seed goes out here, and this frame's
                // own levels are applied on top of it by `on_diff` the moment
                // this returns — so the engine sees Snapshot then Delta, exactly
                // as it did when the Snapshot left at seed time (M5 stage C).
                publish_seed(sink);
                return;
            }
            ++stats_.seed_bracket_failed;
            drop_book(GapReason::SeqGap, sink);
            return;
        }
        if (frame_.first_update_id == last_u_ + 1) { return; }
        // THE TRANSPORT CHECK, and the only thing it is wired to. It caught the
        // deliberate reconnect's 2,204 missed updates; it has never caught a
        // book-correctness defect and must never be quoted as though it could.
        ++stats_.seq_breaks;
        drop_book(GapReason::SeqGap, sink);
    }

    template <typename Sink>
    void apply_levels(const BookLevel* src, std::uint32_t n, Side side, Sink& sink) {
        BookLevel* dst = side == Side::Bid ? bids_ : asks_;
        std::uint32_t& count = side == Side::Bid ? bid_count_ : ask_count_;
        for (std::uint32_t i = 0; i < n; ++i) { apply_level(dst, count, src[i], side, sink); }
    }

    // One wire level onto the ladder. Emits what actually changed within the
    // EMITTED window and nothing that did not.
    template <typename Sink>
    void apply_level(BookLevel* dst, std::uint32_t& count, const BookLevel& lvl, Side side,
                     Sink& sink) {
        const std::uint32_t at = ladder::rank_of(dst, count, lvl.px, side);
        const bool found = ladder::holds(dst, count, at, lvl.px);

        if (lvl.qty == 0) {
            if (!found) {
                // NORMAL, AND CONSTANT. The venue's book is deeper than anything
                // it sends us, so removals for levels outside the seeded window
                // arrive continuously. Counted, never an error, never a Gap.
                ++stats_.levels_absent_removals;
                return;
            }
            ladder::erase_at(dst, count, at);
            ++stats_.levels_removed;
            if (at < kBinanceEmitDepth) {
                emit_delta(lvl.px, 0, side, sink);
                // The erase pulled the level that was at rank kBinanceEmitDepth
                // into the window. The engine has never been told about it.
                if (count >= kBinanceEmitDepth) {
                    const BookLevel& entered = dst[kBinanceEmitDepth - 1];
                    ++stats_.window_entries;
                    emit_delta(entered.px, entered.qty, side, sink);
                }
            }
            return;
        }

        if (found) {
            if (dst[at].qty == lvl.qty) {
                ++stats_.levels_unchanged;
                return;
            }
            dst[at].qty = lvl.qty;
            ++stats_.levels_applied;
            if (at < kBinanceEmitDepth) { emit_delta(lvl.px, lvl.qty, side, sink); }
            else { ++stats_.levels_outside_emit; }
            return;
        }

        // A new price. `displaced` is the level pushed off the END of STORAGE —
        // a 1,024-deep book that is full — which is a different event from a
        // level leaving the emitted window.
        const bool at_emit_edge = count >= kBinanceEmitDepth;
        BookLevel displaced{};
        const bool evicted =
            ladder::insert_at(dst, count, at, lvl, kBinanceMaxFrameLevels, displaced);
        ++stats_.levels_applied;
        if (evicted) { ++stats_.levels_evicted; }

        if (at >= kBinanceEmitDepth) {
            ++stats_.levels_outside_emit;
            return;
        }
        emit_delta(lvl.px, lvl.qty, side, sink);
        if (at_emit_edge) {
            // The insert pushed whatever was last in the window out of it. The
            // engine holds it and must be told it is gone, or its book keeps a
            // level this one no longer shows.
            const BookLevel& exited = dst[kBinanceEmitDepth];
            ++stats_.window_exits;
            emit_delta(exited.px, 0, side, sink);
        }
    }

    // A snapshot level into the ladder. No event and no eviction report: the
    // Snapshot that follows conveys the whole window at once.
    void seed_level(BookLevel* dst, std::uint32_t& count, const BookLevel& lvl,
                    Side side) noexcept {
        if (lvl.qty == 0) { return; }  // a seed does not carry removals
        const std::uint32_t at = ladder::rank_of(dst, count, lvl.px, side);
        if (ladder::holds(dst, count, at, lvl.px)) {
            dst[at].qty = lvl.qty;
            return;
        }
        BookLevel displaced{};
        if (ladder::insert_at(dst, count, at, lvl, kBinanceMaxFrameLevels, displaced)) {
            ++stats_.levels_evicted;
        }
    }

    // ---- the pre-seed buffer ------------------------------------------------

    struct BufferedEvent {
        std::int64_t first_id = 0;
        std::int64_t final_id = 0;
        std::uint32_t bid_at = 0;
        std::uint32_t bid_count = 0;
        std::uint32_t ask_at = 0;
        std::uint32_t ask_count = 0;
    };

    template <typename Sink>
    void buffer_diff(Sink& sink) {
        const std::uint32_t need = frame_.bid_count + frame_.ask_count;
        if (buf_events_ >= kBinanceBufferEvents ||
            buf_levels_ + need > kBinanceBufferLevels) {
            // The connect outran the buffer. Not silent: the events held so far
            // can no longer be reconciled with a snapshot, so the whole attempt
            // restarts. Measured worst case is 15 events / 823 levels against
            // bounds of 64 / 2,048, so this is a real bound rather than a
            // likely one.
            ++stats_.buffer_overflows;
            ++stats_.deltas_before_seed;
            buf_events_ = 0;
            buf_levels_ = 0;
            reseed_wanted_ = true;
            ++stats_.reseeds_requested;
            emit_gap(GapReason::Overflow, sink);
            return;
        }
        BufferedEvent& ev = buf_[buf_events_++];
        ev.first_id = frame_.first_update_id;
        ev.final_id = frame_.final_update_id;
        ev.bid_at = buf_levels_;
        ev.bid_count = frame_.bid_count;
        for (std::uint32_t i = 0; i < frame_.bid_count; ++i) {
            buf_lvl_[buf_levels_++] = frame_.bids[i];
        }
        ev.ask_at = buf_levels_;
        ev.ask_count = frame_.ask_count;
        for (std::uint32_t i = 0; i < frame_.ask_count; ++i) {
            buf_lvl_[buf_levels_++] = frame_.asks[i];
        }
        ++stats_.buffered_events;
    }

    template <typename Sink>
    void apply_buffered(const BufferedEvent& ev, Sink& sink) {
        if (have_last_u_ && bracket_checked_ && ev.first_id != last_u_ + 1) {
            ++stats_.seq_breaks;
            drop_book(GapReason::SeqGap, sink);
            return;
        }
        for (std::uint32_t i = 0; i < ev.bid_count; ++i) {
            apply_level(bids_, bid_count_, buf_lvl_[ev.bid_at + i], Side::Bid, sink);
        }
        for (std::uint32_t i = 0; i < ev.ask_count; ++i) {
            apply_level(asks_, ask_count_, buf_lvl_[ev.ask_at + i], Side::Ask, sink);
        }
        last_u_ = ev.final_id;
        have_last_u_ = true;
        note_depth();
    }

    // ---- reporting ----------------------------------------------------------

    static std::uint32_t emit_count(std::uint32_t held) noexcept {
        return held < kBinanceEmitDepth ? held : kBinanceEmitDepth;
    }

    // How many held levels lie inside the seeded range on one side. `rank_of`
    // returns the index of the first level that does NOT rank better than the
    // boundary, so a level sitting exactly ON the boundary is the one case it
    // excludes and the one case that is inside — hence the `holds` term.
    static std::uint32_t cover(const BookLevel* side, std::uint32_t count,
                               PriceTicks boundary, Side s) noexcept {
        std::uint32_t at = ladder::rank_of(side, count, boundary, s);
        if (ladder::holds(side, count, at, boundary)) { ++at; }
        return at;
    }

    // B1's low-water marks, and B2's. Called once per frame, after the frame's
    // levels have all been applied — a mid-frame reading would report the
    // trough of a coalesced update that never existed as a book anyone saw.
    void note_depth() noexcept {
        if (bid_count_ < stats_.min_bid_levels) { stats_.min_bid_levels = bid_count_; }
        if (ask_count_ < stats_.min_ask_levels) { stats_.min_ask_levels = ask_count_; }
        if (!have_seed_bounds_) { return; }

        const std::uint32_t bc = bid_cover();
        const std::uint32_t ac = ask_cover();
        if (bc < stats_.min_bid_cover) { stats_.min_bid_cover = bc; }
        if (ac < stats_.min_ask_cover) { stats_.min_ask_cover = ac; }

        // THE TRIGGER. An observable, not a timer — and the whole reason it is
        // one is that a constant cannot survive the 5x market-dependent spread
        // `--window-sweep` measured. This asks the market how fast it is going
        // by watching what it consumes.
        if (!cover_trigger_armed_ || cover_trigger_latched_) { return; }
        const std::uint32_t worst = bc < ac ? bc : ac;
        if (worst >= kBinanceReseedCoverLevels) { return; }

        // ONCE PER SEED EPOCH. The adapter cannot fetch and has no clock, so it
        // latches and the transport acts — Kraken's `resync_wanted()` shape.
        // **The rate limit is therefore the transport's and is a required
        // property of it**, exactly like the fetch deadline the margin is sized
        // against: a transport that re-fetches on every frame while coverage
        // stays low would spend 50 weight ten times a second, and the venue bans
        // on breach. This latch bounds it to one request per seed; bounding the
        // seeds is the layer above.
        cover_trigger_latched_ = true;
        ++stats_.cover_triggers;
        reseed_wanted_ = true;
        ++stats_.reseeds_requested;
    }

    SymbolConfig cfg_;
    BinanceFrame frame_{};

    BookLevel bids_[kBinanceMaxFrameLevels]{};
    BookLevel asks_[kBinanceMaxFrameLevels]{};
    std::uint32_t bid_count_ = 0;
    std::uint32_t ask_count_ = 0;

    BufferedEvent buf_[kBinanceBufferEvents]{};
    BookLevel buf_lvl_[kBinanceBufferLevels]{};
    std::uint32_t buf_events_ = 0;
    std::uint32_t buf_levels_ = 0;

    Stats stats_{};
    Seq next_seq_ = 1;
    SeedState seed_ = SeedState::Unseeded;
    std::int64_t last_u_ = 0;
    bool have_last_u_ = false;
    bool bracket_checked_ = false;
    bool reseed_wanted_ = false;

    // The seeded range, and the trigger's two flags. `armed` is a property of
    // the seed (could it ever satisfy the margin); `latched` is a property of
    // this epoch (has the ask already been made).
    PriceTicks seed_bid_floor_ = 0;
    PriceTicks seed_ask_ceil_ = 0;
    bool have_seed_bounds_ = false;
    bool cover_trigger_armed_ = false;
    bool cover_trigger_latched_ = false;
    ParseStatus last_status_ = ParseStatus::Ok;
};

}  // namespace depthcharge::binance
