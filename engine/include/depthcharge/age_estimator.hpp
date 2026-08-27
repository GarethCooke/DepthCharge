// depthcharge/age_estimator.hpp — how far behind the venue is the book on screen?
//
// M4 stage A2. The companion to liveness_clock.hpp and the other half of the
// 2026-08-17 ruling: that ruling took book silence away from the STALENESS clock
// (no threshold on it can be correct, because a quiet market and a dead
// subscription are identical on the wire) and said the panel should show the
// book's age as A NUMBER instead. This computes that number.
//
// ============================================================================
// THE DEFINITION, PINNED BEFORE THE CODE (stage A brief, *Owed by stage B* #8)
// ============================================================================
//
//   `age_ms` is the estimated age of the displayed book relative to the venue's
//   current state — that is, QUEUING LAG. It is not time since the last frame,
//   and it is not time since the book last changed. A book that has not changed
//   is not old: at MINA/GBP nobody traded, and the displayed book was exactly
//   correct throughout 25,843 ms of silence. Time since the last change is
//   market information and may be displayed, but it is not age and must never
//   drive a rendered state.
//
// The three meanings coincide at Anvil-with-a-busy-feeder, which is why they
// were treated as one for three milestones. They diverge by orders of magnitude:
// time since the last book FRAME is ~80 ms at Anvil always (12.6/s even with the
// feeder switched off — anvil_101_feederoff_20260817); time since the book last
// CHANGED is unbounded at both venues; queuing lag is zero until a socket
// backlogs and then 111 s.
//
// ============================================================================
// WHY A DEFICIT AGAINST THE LIVENESS SIGNAL MEASURES IT
// ============================================================================
//
// The liveness signal is the one thing on the wire emitted by a CLOCK rather
// than by the market — Anvil's `summary` on a fixed engine-thread deadline that
// fires on an empty queue, Kraken's 1 Hz heartbeat. So over a window of wall
// time W the venue emitted W / interval of them. If we received fewer, and if
// the missing ones are QUEUED rather than dropped, then the point in the stream
// we have reached is behind the point the clock has reached, and the difference
// is the age of the book in front of you.
//
// **THE PREMISE, AND IT IS A PROPERTY OF THE VENUE RATHER THAN OF ARITHMETIC.**
// A deficit is an age only if the venue QUEUES. If it SHEDS, the identical
// deficit appears and there is no lag at all — rate alone cannot tell the two
// apart. At Anvil the premise is measured, not assumed:
// `CrowWsSubscriber::deliver()` appends to an unbounded per-connection buffer
// with no cap, no drop rule and no coalescing (read in Anvil's source
// 2026-08-16, and `PROTOCOL.md` §4 now states it as contract), and
// `tools/anvil_freshness_probe.py` confirmed it end to end on 2026-08-11 — a
// socket throttled to 25% saw EVERY frame kind thinned by the same fraction and
// its lag grow linearly to 111 s over 150 s with no plateau.
//
// **At Kraken the premise is UNPROVEN and this file does not assume it.** The
// arithmetic is venue-free; whether the answer is an age at Kraken is a question
// for stage B/D, and it is recorded as owed rather than quietly inherited.
//
// ============================================================================
// THE INTERVAL THE DEFICIT IS TAKEN AGAINST — AND WHY IT IS *NOT* THE ROLLING
// MEDIAN THE GREY THRESHOLD USES
// ============================================================================
//
// This is the one decision in this file that is not arithmetic, and getting it
// wrong produces a confidently wrong number rather than a missing one.
//
// The threshold clock beside this one (liveness_clock.hpp) uses a ROLLING median
// and must: a feed that has permanently halved its rate should not cry wolf
// forever, so the thing that decides *is this feed still doing what it has been
// doing* has to adapt. **An age computed against that same rolling median is
// blind to the exact failure it exists to catch.** Work it through: a socket
// throttled to a quarter of the broadcast rate sees its rolling median settle at
// four times the interval within one window, after which `elapsed - n x median`
// is ZERO — so the meter would report a peak while the median was adapting and
// then fall back to 0.0 s while the true lag climbed to 111 s. That is precisely
// the reading M3 stage E was built to make impossible: a feed running perfectly,
// parsing perfectly, and showing a book a hundred seconds behind the market.
//
// So the age is taken against a BASELINE interval, latched once per connection
// and never adapted:
//
//   **A fresh socket's server-side send queue is EMPTY, so the cadence observed
//   at the start of a connection is the broadcast cadence.** That is the only
//   moment a single client can measure the venue's clock, and it is why the
//   baseline is learned there and frozen — after that the queue is a confounder
//   and every later measurement is of the queue rather than of the venue.
//
// It also satisfies the constraint the ruling put on constants, and by the same
// route: `PROTOCOL.md` §3.5 says a client "must derive it from the cadence it
// observes on the connection it is using, not from a number read out of this
// document". The baseline is derived, per connection, from observation. Nothing
// here is hardcoded — `kSummaryPeriodUs = 500000` in staleness.hpp is exactly
// what this replaces.
//
// **THE BLIND SPOT THIS LEAVES, STATED RATHER THAN DISCOVERED, AND IT IS A
// PROOF RATHER THAN AN OVERSIGHT.** A connection that was ALREADY behind at the
// instant it started measuring latches a slow baseline and reports no lag. From
// one socket that case is not identifiable at all: "the venue broadcasts at 2 Hz
// and I am two minutes behind" and "the venue broadcasts at 0.5 Hz and I am
// current" produce byte-identical wire behaviour, and no amount of arithmetic
// separates them. Two things bound it:
//
//   * server-side it cannot happen — a queue that has not been created yet
//     cannot be full — so what the blind spot really covers is a CLIENT too slow
//     to drain the wire from birth. The board's own RX loop was instrumented for
//     exactly this over the 23.6 h soak and exonerated: `wait 0 / read 98-99 /
//     feed 0` in every hour;
//   * `_local/drain-120ms.ndjson` and the 2026-08-11 desk probe are both this
//     shape by construction — the capture tool sleeps from the first message —
//     so both are inside the blind spot, and `test_age_estimator.cpp` pins that
//     as a KNOWN LIMIT rather than letting a future reader discover it as a bug.
//
// What closes it is not a better estimator: it is the client PING, which prices
// this socket's server-side queue directly by measuring how far behind a
// backlog a pong arrives (Crow answers unconditionally and appends the pong to
// the same per-connection buffer as data, so it cannot overtake). `PingProbe`
// exists already (D5); using the two together is deferred to whichever milestone
// next opens the transport layer — M6 on current shape — deliberately and with
// the reason recorded (M4 triage, decision (a)).
//
// ============================================================================
// SLIDING WINDOW, NOT CUMULATIVE — THE CONSTRAINT THAT ARRIVED BEFORE THE CODE
// ============================================================================
//
// The obvious implementation anchors at connect and integrates forever:
// `elapsed_since_anchor - received x interval` (that is `firmware/src/
// staleness.hpp`, M3 stage E, and it is superseded by this file — see BELOW).
// It has two failures a window does not:
//
//   1. **IT NEVER FORGETS A BIAS.** Every liveness frame that fails to parse,
//      or is lost, or arrives while the estimator is not looking, adds one
//      interval to the reported age FOREVER. staleness.hpp names this bias and
//      can only ask the reader to cross-check the parse counters.
//   2. **IT AVERAGES AWAY STALL-THEN-BURST.** The failure shape at a queueing
//      venue is a pause followed by a flood that repays the count, and any
//      estimator whose memory is long enough to contain both reports a healthy
//      average across an outage that really happened.
//
// So the window is sized to catch stall-then-burst rather than average it away,
// and the estimator is the SUPREMUM OF THE DEFICIT OVER EVERY SUFFIX of the
// window rather than one deficit from one anchor:
//
//     age(now) = max over retained arrivals i of
//                    [ (now - t_i) - (arrivals after i) x baseline_interval ]
//                clamped at zero, which is the i = now term
//
// That is Lindley's recursion for a queue's backlog, truncated to a window, and
// the three properties that matter fall straight out of it:
//
//   * **it returns to zero when the socket catches up**, because the suffix
//     starting at the newest arrival is always available and scores zero;
//   * **it cannot ratchet**, because a bias ages out of the window instead of
//     being carried for the life of the connection;
//   * **it reports the peak of a stall while the burst is still arriving**,
//     because the suffix that starts before the stall is still in the window.
//
// ITS CEILING, STATED RATHER THAN DISCOVERED. The window holds the last
// `kAgeWindowSamples` arrivals and nothing older, so the largest age it can
// report is the lag ACCUMULATED WITHIN THAT WINDOW. At a drain fraction f the
// window spans `N x interval / f` of wall time and the true lag accrued over it
// is `(1 - f) x span` — which is exactly what the sup returns, so the ceiling
// binds only on a backlog older than the window itself. Worked, at N = 256:
//
//     venue / drain       window spans     reportable age
//     Anvil healthy         128 s            ~0 (there is none)
//     Anvil at f = 0.5      256 s            128 s
//     Anvil at f = 0.25     512 s            384 s
//
// **A TOTAL SILENCE HAS NO CEILING, AND THAT IS WORTH SEPARATING FROM THE ROW
// ABOVE.** The window is a window over ARRIVALS, not over time, so when nothing
// arrives it stops refilling: the oldest retained sample stays put and
// `(now - t_i)` grows without bound. Anvil's 176,000 ms silence of 2026-08-16
// (A2b) is that shape — the largest real fault on record — and it would be
// reported in full, at any prior drain fraction. The ceiling binds only on a
// SUSTAINED PARTIAL drain, where arrivals keep pushing the window forward.
//
// The cost is 2 KiB of state, which is why the number is 256 and not 4,096.
//
// ============================================================================
// WHAT THIS NUMBER IS NOT ALLOWED TO DO
// ============================================================================
//
// **Nothing branches on it** (invariant #5, and the ruling's point 4): age is
// rendered as a number and NEVER as grey. Grey belongs to the liveness clock,
// which counts a different signal for a different reason. An age that greyed the
// panel would put a threshold back on a quantity the ruling removed one from.
//
// **It has a one-interval sawtooth and that is its resolution.** Between two
// liveness arrivals the elapsed term grows while the delivered term does not, so
// a socket that is perfectly current oscillates between 0 and one interval —
// 500 ms at Anvil, 1,000 ms at Kraken. Anything under one interval is noise, and
// the renderer's tenths are honest about that rather than hiding it.
//
// **It dies with the socket.** The backlog is server-side state on ONE
// connection; a reconnect gets a fresh queue and a fresh snapshot, so the
// estimate resets on `Gap{Disconnect}` and the peak is banked on the way down
// (the same defect staleness.hpp shipped with for one afternoon: high-water
// marks written only on arrival lose the whole episode when the socket drops).
//
// ============================================================================
// THE COVERAGE GAP, NAMED BECAUSE INVARIANT #6 CANNOT BE SATISFIED HERE
// ============================================================================
//
// **No committed trace can prove this estimator right, and no trace ever will
// be able to.** A backlog is a property of ONE CLIENT'S SOCKET, not of the wire:
// two sockets on the same server at the same instant disagree about the book's
// age, and a capture's `rx_ns` records only when THIS client got the bytes. The
// 111 s figure exists only because `anvil_freshness_probe.py` seq-matched a
// throttled socket against an unthrottled one — two sockets, which a trace file
// is not. So the goldens here pin the ARITHMETIC against synthesised arrival
// patterns whose true answer is known by construction, and the one real
// backlogged capture (`_local/drain-120ms.ndjson`) is a measurement reported in
// the session log rather than a golden. Stated in stage A2's DoD rather than
// left to look covered.
//
// ============================================================================
//
// ESP-IDF-free and allocation-free, like liveness_clock.hpp beside it and for
// the same reason: at M4 stage D (2026-08-20) this moved into `engine/` and the
// firmware links it, **deleting `firmware/src/staleness.hpp` outright** — that
// file's cumulative estimator and its hardcoded `kSummaryPeriodUs = 500000` were
// both things the ruling forbids (the interval is operator configuration
// DepthCharge cannot read back). Until that deletion the host and the target
// computed the book's age two different ways, agreeing only because Anvil's
// broadcast interval happened to be the hardcoded 500 ms — the coincidence class
// (ARCHITECTURE §9, 2026-08-18), and a divergence no host test could see. It is
// closed by deletion, which was always the only way it could close (DESIGN §08
// strain 23). Every reference to `staleness.hpp` below is to that deleted file
// and is kept because the contrast is why this arithmetic has the shape it has.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#include <depthcharge/liveness_clock.hpp>
#include <depthcharge/sample_window.hpp>

namespace depthcharge {

// The window, in liveness arrivals. See ITS CEILING above for what the number
// buys; 256 x 8 B = 2 KiB, which is the whole reason it is not larger.
//
// ===========================================================================
// 256 STAYS, AND IT IS NOT PER VENUE — DECIDED AT M5 STAGE C, ON B2's FIGURES
// ===========================================================================
//
// The question handed here was blunt: 256 arrivals is 128 s at Anvil, 256 s at
// Kraken and **85.2 min at Binance**, and the constant's own comment says it is
// a 2 KiB memory budget and never a duration (ARCHITECTURE §9, 2026-08-26). Is
// an 85-minute supremum window defensible at a 20 s cadence?
//
// **YES, AND THE REASON IS THE DIRECTION OF THE ERROR.** This window bounds the
// estimator's REACH, not its accuracy: ITS CEILING above works out that the sup
// binds only on a backlog OLDER THAN THE WINDOW ITSELF, so a wider window can
// only let the meter describe an older backlog, never overstate a newer one. At
// Binance, wider is therefore strictly the safe direction, and the number that
// would need justifying is a SMALLER one — a window that stopped being able to
// characterise a fault that had been running for an hour. Shrinking it per venue
// would buy back at most 2 KiB and cost exactly that.
//
// **AND IT IS NOT THE CONSTANT THAT BITES.** Of the three sized in arrivals, the
// one with a cost at this venue is `kBaselineSamples` — 638.8 s during which the
// meter reads nothing at all — and that one cannot be shrunk either, because 32
// is what a MEASUREMENT set (see below: n=8 manufactured 4.4% of wall-clock as
// phantom lag on the reconnect capture). So the honest answer to the class §9
// named is: state the duration at every venue, which the sentence below now
// does, and move only the constant whose cost is on the wrong side of the
// trade. Neither of these two is.
//
// The interaction is the part worth carrying rather than either figure alone: at
// Binance the meter cannot read for the first ~11 minutes and then holds a
// window 85 minutes wide, so the shortest fault it can characterise and the
// longest it can hold are both set by a count of arrivals. Pinned as behaviour
// by `test_binance_adapter.cpp`'s no-reading case, with an Anvil control.
inline constexpr std::size_t kAgeWindowSamples = 256;

// How many intervals the baseline is measured over, and it is deliberately NOT
// `kMinSamples`, which is what the grey threshold calibrates on. Two gates, two
// questions — and this one was set by a measurement rather than by symmetry.
//
// **A STREAM RESUMES WITH A BURST, AND A SHORT LATCH SWALLOWS IT.** Measured on
// `anvil_101_reconnect`, the first intervals after the socket comes back are
// `253 871 174 475 480 527 478 ...` ms against a 500 ms broadcast — the queue
// flushing, plus a connect that lands at a uniformly random phase. Lower medians
// of the first n intervals: **n=8 -> 478.0 ms, n=16 -> 495.6, n=32 -> 499.3**,
// against a whole-trace median of 499.9.
//
// A LOW BASELINE MANUFACTURES AGE, at a constant rate, for ever: if the
// reference is 4.4% below the true broadcast interval then a perfectly healthy
// feed accrues 4.4% of wall-clock as phantom lag — 4.4 s per 100 s — and the
// panel cries wolf about a socket that is exactly current. That is what n=8
// produced here (a 2.8 s reading on a healthy connection), and it is why this is
// a full window: four outliers cannot move the rank past the middle of 32.
//
// The cost is that the meter reads "-" for the first 16 s of an Anvil connection,
// 32 s of a Kraken one, and **638.8 s — over ten minutes — of a Binance one**.
// That is the honest direction: no reading is a smaller claim than a wrong one.
//
// **THE THIRD FIGURE WAS ADDED AT M5 STAGE B2, AND THE SENTENCE WITHOUT IT IS THE
// POINT.** It was written when two venues existed, and it is a per-venue COST
// stated in a venue-agnostic file — so it went stale the moment a venue arrived
// whose cadence is forty times Anvil's, and nothing anywhere failed. 32 intervals
// is 16 s at 500 ms and 638.8 s at the measured 19,963.97 ms. This constant is
// one of three sized in ARRIVALS rather than in time; the other two are
// `kMinSamples` (8 -> 4 s / 8 s / 159.7 s) and `kAgeWindowSamples` (256 ->
// 128 s / 256 s / **85.2 min**, which is the span of the window the supremum
// above is taken over and which nobody has sized at this venue). The class is in
// ARCHITECTURE.md 9, 2026-08-26: a sample count in a venue-agnostic object must
// state its duration at every venue the object serves. **What to DO about any of
// the three is C's**, with the threshold work; this line only stops the cost
// being invisible.
//
// The opposite bias — a backlog building DURING the latch window, dragging the
// reference up — under-reports the age, which is the safe direction and is
// already the documented blind spot rather than a new one.
inline constexpr std::size_t kBaselineSamples = kWindowSamples;

// An estimate, or the honest absence of one.
//
// TWO STATES, DELIBERATELY. "No reading yet" and "the book is current" are
// different statements and exactly one of them is reassuring — printing 0.0 s
// before the window has calibrated would be the wrong one. Same rule as
// staleness.hpp's `anchored()`.
struct AgeReading {
    bool valid = false;
    std::uint32_t ms = 0;
};

// Milliseconds as a compact unit-bearing token: "0.5s", "12.3s", "1m51s",
// "2h56m".
//
// **IT RENDERS TO MINUTES AND HOURS, AND THAT IS A REQUIREMENT RATHER THAN A
// NICETY.** The precedent is this project's own: `SecondsText` took uint32
// microseconds, which is 71.6 minutes, and the 23.6 h soak of 2026-08-15/16
// printed `4294.9 s` for eleven straight hours while the true age climbed past
// it — three of four connection segments hit the wall. A seconds-only format
// does the same thing in a softer way: `6821.4 s` is a number a reader has to do
// arithmetic on at the exact moment they are trying to read a verdict off a
// line. uint32 milliseconds tops out at 49.7 days and prints as `1193h02m`.
//
// One printf ARGUMENT rather than two, for the reason the histogram renderer and
// SecondsText are both written that way: the failure mode of `%u.%01u` fed from
// two expressions is not a crash, it is a label printed beside the neighbouring
// field's number, which reads perfectly and is wrong.
struct AgeText {
    char buf[16]{};

    explicit AgeText(std::uint32_t ms) noexcept {
        if (ms < 60000u) {
            std::snprintf(buf, sizeof buf, "%u.%us", ms / 1000u, (ms / 100u) % 10u);
        } else if (ms < 3600000u) {
            std::snprintf(buf, sizeof buf, "%um%02us", ms / 60000u, (ms / 1000u) % 60u);
        } else {
            std::snprintf(buf, sizeof buf, "%uh%02um", ms / 3600000u, (ms / 60000u) % 60u);
        }
    }

    // The absent reading, spelled once so every caller spells it the same way.
    static AgeText unknown() noexcept { return AgeText{}; }

    // AND THE TERNARY THAT CHOOSES BETWEEN THEM, ALSO SPELLED ONCE. Added at
    // M4 stage D after review found `r.valid ? AgeText(r.ms) : AgeText::unknown()`
    // written out at four call sites, one of which had already drifted to a
    // hand-written "-". The failure mode of a fifth caller forgetting the
    // ternary is not a crash: it is `0.0s` printed for a connection that has no
    // reading yet, which is the one reassuring answer the two-state design
    // exists to withhold.
    static AgeText from(const AgeReading& r) noexcept {
        return r.valid ? AgeText(r.ms) : unknown();
    }
    static AgeText from(bool valid, std::uint32_t ms) noexcept {
        return valid ? AgeText(ms) : unknown();
    }

private:
    AgeText() noexcept { buf[0] = '-'; buf[1] = '\0'; }
};

// The windowed deficit. Feed it the venue's liveness arrivals; it measures this
// connection's baseline from the first `kMinSamples` intervals of them and holds
// that until the socket dies.
//
// IT MEASURES ITS OWN BASELINE RATHER THAN BORROWING THE THRESHOLD CLOCK'S
// ROLLING MEDIAN, AND THE DIFFERENCE ONLY SHOWS UP AT A RECONNECT — which is
// where it would have been a silent wrong answer. `LivenessClock`'s window
// spans the last 32 intervals and is deliberately NOT reset when a socket drops,
// because cadence is a property of the venue rather than of the connection. A
// baseline taken from it would therefore inherit up to 32 intervals of the DEAD
// connection: reconnect away from a backlogged socket, and the fresh healthy one
// latches the throttled cadence and reports no lag for ever. The two objects
// measure the same signal and must extract different quantities from it — a
// rolling median for "is this feed still doing what it has been doing", and a
// once-per-connection reference for "how far behind that am I now". The ring and
// the median convention are shared (sample_window.hpp), so what differs between
// them is only what is supposed to.
class AgeEstimator {
public:
    // One liveness-signal arrival, at the capture/board clock's nanoseconds.
    //
    // The baseline latches on the `kBaselineSamples`-th INTERVAL of the
    // connection — early, because every interval after it is one in which a
    // backlog could be biasing the number that is supposed to detect the
    // backlog, and no earlier, because a stream resumes with a burst that a
    // short window would swallow (see kBaselineSamples).
    void on_liveness(std::int64_t rx_ns) noexcept {
        arrivals_.push(rx_ns);
        if (baseline_ms_ <= 0.0 && arrivals_.size() > kBaselineSamples) {
            baseline_ms_ = first_window_median_ms();
        }
    }

    // The socket died. The queue died with it, so the estimate must too — but
    // bank the peak first, or the one event the instrument exists to capture is
    // the one that erases it. The baseline goes with it: the next connection
    // measures the venue's clock again from an empty queue, which is the only
    // moment it can be measured at all.
    void on_reconnect(std::int64_t at_ns) noexcept {
        bank(read(at_ns));
        arrivals_.clear();
        baseline_ms_ = 0.0;
    }

    // THE SOCKET DID NOT DIE — IT WENT QUIET AND CAME BACK, and that is a
    // different event which must NOT discard the baseline. Added at M4 stage D
    // after review.
    //
    // The distinction is the one this file already argues at length under THE
    // BLIND SPOT: a baseline is only measurable on a connection whose
    // server-side queue is EMPTY, which is true exactly once, at connect. A
    // watchdog firing on a live socket is not that moment. Re-latching there
    // measures the DRAIN, not the cadence — and the drain is a burst.
    //
    // Worked through, because the failure is silent and permanent: Anvil queues
    // and never drops per socket, so a 40 s stall accumulates ~80 `summary`
    // frames that flush in ~200 ms on recovery, i.e. ~2.5 ms apart. A baseline
    // re-latched from those reads ~2.5 ms instead of 500 ms, and every age
    // afterwards is `elapsed - n x 2.5 ms` — which on a perfectly healthy feed
    // accrues 99.5% of wall-clock as phantom lag, for the life of the
    // connection. The panel would read minutes behind while being current.
    //
    // So this banks the peak and voids the current READING — the arrivals are
    // cleared because the estimator genuinely cannot say whether the frames it
    // missed were queued or never sent — and leaves the baseline alone, because
    // the venue's cadence has not changed and this connection already measured
    // it at the only moment it could.
    void on_stall(std::int64_t at_ns) noexcept {
        bank(read(at_ns));
        arrivals_.clear();
    }

    // The current estimate. Const: a reader can ask without moving anything.
    AgeReading read(std::int64_t now_ns) const noexcept {
        if (baseline_ms_ <= 0.0) { return AgeReading{}; }
        // AN EMPTY WINDOW IS "NO READING", NOT ZERO. Reachable only through
        // `on_stall`, which keeps the baseline and drops the arrivals: the sup
        // below would then run over nothing and return the clamp, claiming the
        // book is current at the exact instant the watchdog greyed the panel.
        // Same rule, same reason, as the pre-baseline case one line up.
        if (arrivals_.size() == 0) { return AgeReading{}; }

        // The sup over every suffix of the window. The i = now suffix scores
        // zero and is the clamp, so `best` starts there and the answer is never
        // negative — a socket that is AHEAD of the clock is not a book from the
        // future, it is a burst draining, and zero is the honest reading.
        const std::size_t n = arrivals_.size();
        double best = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double elapsed_ms = static_cast<double>(now_ns - arrivals_.at(i)) / 1e6;
            const double delivered_ms = static_cast<double>(n - 1 - i) * baseline_ms_;
            best = std::max(best, elapsed_ms - delivered_ms);
        }
        return AgeReading{true, clamp_ms(best)};
    }

    // read(), and remember the peak. This is what the feed side calls once per
    // publish; `read` is for anyone who must not disturb the high-water mark.
    AgeReading read_and_bank(std::int64_t now_ns) noexcept {
        const AgeReading r = read(now_ns);
        bank(r);
        return r;
    }

    // The largest age seen this run, across reconnects. The per-connection
    // figure is destroyed every time the socket blinks, and that erasure is
    // exactly how the 86-minute run of 2026-08-09 looked healthy.
    std::uint32_t worst_ms() const noexcept { return worst_ms_; }

    // This connection's reference cadence, or 0 before it latched. Reported
    // wherever an age is reported: every age is `elapsed - n x baseline`, so a
    // reader who cannot see the baseline cannot check the arithmetic — and a
    // baseline that is not the venue's broadcast interval is the one way this
    // instrument lies (see THE BLIND SPOT above).
    double baseline_ms() const noexcept { return baseline_ms_; }

    std::size_t samples() const noexcept { return arrivals_.size(); }
    bool calibrated() const noexcept { return baseline_ms_ > 0.0; }

private:
    // The median of this connection's first `kBaselineSamples` intervals. Called
    // once per connection, at the moment the ring first holds enough of them, so
    // it reads the OLDEST samples rather than the newest — which is the whole
    // point of latching early.
    double first_window_median_ms() const noexcept {
        std::array<double, kBaselineSamples> gaps{};
        for (std::size_t i = 0; i < kBaselineSamples; ++i) {
            gaps[i] = static_cast<double>(arrivals_.at(i + 1) - arrivals_.at(i)) / 1e6;
        }
        return lower_median(gaps.data(), kBaselineSamples);
    }

    void bank(const AgeReading& r) noexcept {
        if (r.valid && r.ms > worst_ms_) { worst_ms_ = r.ms; }
    }

    static std::uint32_t clamp_ms(double ms) noexcept {
        constexpr double kMax = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        if (ms <= 0.0) { return 0; }
        return ms >= kMax ? std::numeric_limits<std::uint32_t>::max()
                          : static_cast<std::uint32_t>(ms);
    }

    SampleRing<std::int64_t, kAgeWindowSamples> arrivals_;
    double baseline_ms_ = 0.0;   // this connection's reference cadence; 0 = not yet latched
    std::uint32_t worst_ms_ = 0;
};

}  // namespace depthcharge
