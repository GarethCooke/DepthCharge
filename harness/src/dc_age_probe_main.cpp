// dc_age_probe_main.cpp — what does `age_ms` read through a KNOWN backlog?
//
// M5 stage B2, section 5, and it is the same move M4 stage B2 section 4 made at
// Kraken: settle a venue assumption by measurement rather than carry it forward.
// `tools/kraken_backpressure_probe.py` did it on the wire with a live socket;
// this does it on the desk with a committed trace, because the question here is
// not about the venue's send queue at all — it is about **which subsystem emits
// the signal the meter counts**, and that is answerable from a file.
//
// ===========================================================================
// THE QUESTION, AND WHY IT HAS TWO ANSWERS RATHER THAN ONE
// ===========================================================================
//
// `age_ms` is a sliding-window deficit against the venue's liveness arrivals
// (age_estimator.hpp). Over a window of wall time W the venue's clock emitted
// W / interval of them; if fewer arrived and the missing ones are QUEUED, the
// point in the stream we have reached is behind the point the clock has reached,
// and the difference is the age of the book on screen.
//
// ARCHITECTURE §9's 2026-08-25 stage-B1 row states the hypothesis this program
// exists to test: at Anvil and Kraken the liveness record and the book come from
// the SAME subsystem, so *the feed is alive* and *the socket is alive* coincide;
// at Binance the ping is emitted by the WebSocket layer BELOW the subscription,
// on a fixed ~20 s timer, so it proves the socket and never the feed. A signal
// that cannot itself fall behind cannot show a deficit — **so `age_ms` should
// read approximately zero through an arbitrarily large book backlog.**
//
// That hypothesis is right about the mechanism and imprecise about the scope,
// and the imprecision matters because it is the difference between "the meter is
// broken here" and "the meter answers a narrower question here". There are two
// backlogs and they are not the same event:
//
//   SOCKET BACKLOG — this client, or the path to it, cannot drain the wire.
//     The ping is a WebSocket control frame on the same TCP stream as the depth
//     frames, so it is delayed by exactly the same backlog. Arrivals stretch,
//     the deficit appears, and the meter reads the lag. **Physical at all three
//     venues, and the meter works at all three.**
//
//   FEED BACKLOG — the venue's own market-data publisher falls behind, or the
//     subscription stops, while the WebSocket layer keeps its 20 s timer. The
//     book ages and the ping does not. **Physical at Binance and NOT
//     CONSTRUCTIBLE at Anvil or Kraken**, where the thing that emits the
//     liveness record is the thing that emits the book. This is the mode in
//     which the meter is structurally blind, and Binance is the first venue at
//     which the mode exists at all.
//
// So this program runs both, on any trace, and reports the meter's reading
// against the lag it INJECTED — which a replay knows exactly, and which is what
// `anvil_freshness_probe.py` needed two sockets to obtain.
//
// ===========================================================================
// AND A FINDING THAT ARRIVES BEFORE EITHER MODE DOES
// ===========================================================================
//
// The baseline latches on the `kBaselineSamples`-th interval — 32 — and at a
// ~19,970 ms ping cadence that is **~639 s of wall clock after the first ping**,
// so ~11 minutes after connect. Anvil reaches it in 16 s and Kraken in 32 s.
// **No capture this project has ever taken is that long**, and the longest is
// this stage's own 221 s calibration trace. So on every committed Binance file
// the meter reads `-` from the first record to the last, in both modes, for a
// reason that has nothing to do with queues.
//
// That is reported first and separately, because it is the state the panel will
// actually be in for the first eleven minutes of every Binance connection and
// nothing anywhere had named it.
//
// ===========================================================================
// WHAT IS MEASURED AND WHAT IS SYNTHESISED, SAID OUT LOUD
// ===========================================================================
//
// The ARRIVAL CADENCE is measured, from the committed trace named on the command
// line. The LENGTH is synthesised when `--extend-to` asks for more wall clock
// than the trace holds, because no capture reaches the baseline and taking an
// 11-minute one to watch a constant tick would be paying for a number the
// cadence already gives. Synthetic arrivals continue at the trace's own measured
// median and the report says how many of each it used — a run whose verdict
// rests on synthesised input must say so on the same line as the verdict.
//
// This is ARCHITECTURE §9's 2026-08-18 rule: where the code and every available
// file agree, synthesise the input that discriminates.
//
// The clock is `event_ns` throughout — arrivals and `now` alike — which is the
// instant a record describes rather than the instant the capture tool flushed
// it. `replay_driver.cpp` feeds the age estimator `rx_ns` instead, deliberately,
// because it reads the deficit against `current_rx_ns_` and mixing the two in
// one subtraction can go negative. The two are the same number on the board,
// where nothing writes a trace file; they differ only in replay, and at Binance
// they differ a lot (three pings 20 s apart share one `rx_ns` in the silent
// stream fixture). Using `event_ns` on both sides here is the board-equivalent
// measurement and the difference is reported.
//
//   usage: dc_age_probe <trace.ndjson> [--factor F] [--from S] [--extend-to S]
//          dc_age_probe --check <trace.ndjson>...
//
// Exit 0 unless `--check` finds a pinned verdict has moved.
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include <depthcharge/age_estimator.hpp>
#include <depthcharge/sample_window.hpp>

#include "dc_harness/trace.hpp"
#include "dc_harness/trace_decoder.hpp"
#include "dc_harness/venue.hpp"

namespace {

// The basename, so a pin row is keyed by the trace and not by where it was
// invoked from — the same helper, and the same reason, as dc_taxonomy_main.cpp.
// ctest passes an absolute Windows path.
std::string basename_of(const std::string& path) {
    const std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

using depthcharge::AgeEstimator;
using depthcharge::AgeReading;
using depthcharge::kBaselineSamples;

// One run's answer. `injected_ms` is the lag this program put into the stream
// and therefore knows exactly; `peak_ms` is what the estimator made of it.
struct Verdict {
    bool calibrated = false;
    double baseline_ms = 0.0;
    double injected_ms = 0.0;
    double peak_ms = 0.0;
    std::size_t arrivals = 0;
};

// The liveness arrival times of a trace, on the `event_ns` clock, plus the
// median interval between them. Everything below is arithmetic on this vector.
struct Arrivals {
    std::vector<std::int64_t> t;
    double median_ms = 0.0;
    dc::harness::Venue venue{};
};

// THE SHARED MEDIAN, AND THIS FILE MUST NOT CARRY ITS OWN.
//
// `sample_window.hpp` says the convention "matters enough to have one home": a
// LOWER median by nearest rank, because an interpolated one invents an interval
// that never occurred on the wire. The first draft of this program wrote its own
// interpolated median, and on the 221 s calibration trace the two answered
// **19,964.0 ms and 19,969.4 ms** for the same ten intervals — which is how
// this file came to quote a cadence the clock it is measuring does not produce.
// Caught before it shipped; the general form is in ARCHITECTURE §9. The other
// copy lived in `harness/src/trace.cpp` for three milestones after this one was
// fixed and was closed at the M5 close-out (2026-09-06), so this call is now
// the ordinary case rather than the exception this comment was written to be.
double median_of(std::vector<double> v) {
    return depthcharge::lower_median(v.data(), v.size());
}

Arrivals read_arrivals(const std::string& path) {
    dc::harness::TraceReader reader(path);
    Arrivals a;
    a.venue = reader.meta().venue;
    dc::harness::RecordClassifier classifier(a.venue);
    dc::harness::TraceRecord rec;
    while (reader.next(rec)) {
        if (classifier.classify(rec).is_liveness) { a.t.push_back(rec.event_ns); }
    }
    std::vector<double> gaps;
    for (std::size_t i = 1; i < a.t.size(); ++i) {
        gaps.push_back(static_cast<double>(a.t[i] - a.t[i - 1]) / 1e6);
    }
    a.median_ms = median_of(gaps);
    return a;
}

// WHEN THE BASELINE LATCHES, in seconds after the first arrival. The estimator
// takes it on the `kBaselineSamples`-th interval, i.e. the
// (kBaselineSamples+1)-th arrival.
//
// **THE THROTTLE MUST START AFTER THIS, AND THE REASON IS THE WHOLE EXPERIMENT.**
// A connection that is ALREADY behind when it starts measuring latches the
// throttled cadence as its reference and reports no lag for ever —
// age_estimator.hpp names that as a proof rather than an oversight, and
// `_local/drain-120ms.ndjson` is inside it by construction. Throttling from t+60
// at this venue lands squarely in the blind spot, because 60 s is nowhere near
// the 639 s the baseline needs, and the run then measures the blind spot instead
// of the question. `kraken_backpressure_probe.py` ran a clean baseline phase
// first for exactly this reason; at Binance that phase is ELEVEN MINUTES, which
// is a finding in its own right rather than a parameter.
double baseline_latch_s(const Arrivals& a) {
    if (a.t.size() <= kBaselineSamples) { return -1.0; }
    return static_cast<double>(a.t[kBaselineSamples] - a.t.front()) / 1e9;
}

// Continue the series at its own measured cadence until `to_s` of wall clock has
// elapsed since the first arrival. Returns how many were real.
std::size_t extend(Arrivals& a, double to_s) {
    const std::size_t real = a.t.size();
    if (a.t.size() < 2 || a.median_ms <= 0.0 || to_s <= 0.0) { return real; }
    const std::int64_t step = static_cast<std::int64_t>(a.median_ms * 1e6);
    const std::int64_t end = a.t.front() + static_cast<std::int64_t>(to_s * 1e9);
    while (a.t.back() < end) { a.t.push_back(a.t.back() + step); }
    return real;
}

// HOW MUCH LAG THE THROTTLE PUTS IN, and it is ONE expression because both modes
// report the same quantity and two routes to one number is how they come to
// disagree. A consumer draining at `factor` of the broadcast rate over a stretch
// of stream `span` falls behind by `span x (1/factor - 1)`.
double injected_lag_ms(const Arrivals& a, double from_s, double factor) {
    if (a.t.size() < 2 || factor <= 0.0) { return 0.0; }
    const double span_after_ms =
        static_cast<double>(a.t.back() - a.t.front()) / 1e6 - from_s * 1e3;
    return span_after_ms > 0.0 ? span_after_ms * (1.0 / factor - 1.0) : 0.0;
}

// ---------------------------------------------------------------------------
// THE TWO MODES
// ---------------------------------------------------------------------------

// SOCKET BACKLOG. Everything on the stream is delayed, the ping included,
// because it is a control frame on the same TCP connection. Arrival i after the
// throttle starts is re-timed to `start + (t_i - start) / factor`, which is what
// a consumer draining at `factor` of the broadcast rate experiences, and `now`
// moves with it. Physical at all three venues.
Verdict run_socket(const Arrivals& a, double from_s, double factor) {
    AgeEstimator est;
    Verdict v;
    v.arrivals = a.t.size();
    const std::int64_t start =
        a.t.front() + static_cast<std::int64_t>(from_s * 1e9);
    for (std::int64_t t : a.t) {
        const std::int64_t at =
            t <= start
                ? t
                : start + static_cast<std::int64_t>(
                              static_cast<double>(t - start) / factor);
        est.on_liveness(at);
        const AgeReading r = est.read(at);
        if (r.valid && static_cast<double>(r.ms) > v.peak_ms) {
            v.peak_ms = static_cast<double>(r.ms);
        }
    }
    v.injected_ms = injected_lag_ms(a, from_s, factor);
    v.baseline_ms = est.baseline_ms();
    v.calibrated = est.baseline_ms() > 0.0;
    return v;
}

// FEED BACKLOG. The book falls behind and the ping does NOT, because at this
// venue it is emitted below the subscription on its own timer. The arrival
// series is therefore untouched and so is `now` — the socket is current, the
// wall clock is current, and only the book is old. The injected lag is what the
// throttle did to the BOOK, which the estimator is never shown.
//
// NOT CONSTRUCTIBLE AT ANVIL OR KRAKEN, where the subsystem that emits the
// liveness record is the subsystem that emits the book: there is no way for one
// to be late and the other current. The arithmetic runs anyway and the report
// says so, because refusing would hide the fact that the arithmetic is identical
// and only the venue's plumbing differs.
Verdict run_feed(const Arrivals& a, double from_s, double factor) {
    AgeEstimator est;
    Verdict v;
    v.arrivals = a.t.size();
    for (std::int64_t t : a.t) {
        est.on_liveness(t);
        const AgeReading r = est.read(t);
        if (r.valid && static_cast<double>(r.ms) > v.peak_ms) {
            v.peak_ms = static_cast<double>(r.ms);
        }
    }
    v.injected_ms = injected_lag_ms(a, from_s, factor);
    v.baseline_ms = est.baseline_ms();
    v.calibrated = est.baseline_ms() > 0.0;
    return v;
}

// WHAT THE METER CAN REPORT AT ALL, which is not the same as the lag that
// exists. `age_estimator.hpp` states the ceiling and works it: the window holds
// `kAgeWindowSamples` ARRIVALS and nothing older, so at a drain fraction f it
// spans `N x interval / f` of wall time and the lag accrued inside it is
// `(1 - f) x span`. Above that the sup has nothing older to measure from.
//
// It is computed here rather than assumed, because a probe that called the
// ceiling a shortfall would report the estimator broken at Anvil and Kraken,
// where a 500 ms and a 1,000 ms cadence put the ceiling BELOW the injected lag.
// At Binance's 20 s cadence the same formula puts it far above — the one thing
// the slow ping buys. Measured against the header's own worked table: it
// predicts 384 s at Anvil and 768 s at Kraken for f = 0.25, and this program
// reads 382.4 s and 765.2 s.
double reportable_ceiling_ms(double baseline_ms, double factor) {
    if (baseline_ms <= 0.0 || factor <= 0.0) { return 0.0; }
    const double span_ms =
        static_cast<double>(depthcharge::kAgeWindowSamples) * baseline_ms / factor;
    return (1.0 - factor) * span_ms;
}

// TRACKS / PARTIAL / BLIND, against what the meter COULD have said.
const char* call_of(const Verdict& v, double factor) {
    if (!v.calibrated) { return "NO READING"; }
    const double ceiling = reportable_ceiling_ms(v.baseline_ms, factor);
    const double expect = v.injected_ms < ceiling ? v.injected_ms : ceiling;
    if (expect <= 1.0) { return "NO BACKLOG"; }
    // Blind: nothing beyond the estimator's own one-interval sawtooth.
    if (v.peak_ms < 2.0 * v.baseline_ms) { return "BLIND"; }
    return v.peak_ms / expect > 0.9 ? "TRACKS" : "PARTIAL";
}

void print_verdict(const char* label, const Verdict& v, bool physical, double factor) {
    if (!v.calibrated) {
        std::printf("  %-16s NO READING — the baseline never latched (%zu arrivals, "
                    "needs %zu)\n",
                    label, v.arrivals, kBaselineSamples + 1);
        return;
    }
    const double ceiling = reportable_ceiling_ms(v.baseline_ms, factor);
    const double expect = v.injected_ms < ceiling ? v.injected_ms : ceiling;
    const double ratio = expect > 1.0 ? v.peak_ms / expect : 0.0;
    const char* call = call_of(v, factor);
    std::printf("  %-16s baseline %8.1f ms   injected %8.1f s   window ceiling "
                "%8.1f s   meter peak %8.1f s   %.2fx of what it could say  => %s%s\n",
                label, v.baseline_ms, v.injected_ms / 1000.0, ceiling / 1000.0,
                v.peak_ms / 1000.0, ratio, call,
                physical ? "" : "  (not a physical state at this venue)");
}

// THE EXPERIMENT'S SHAPE, spelled once. Latch the baseline on clean stream, then
// throttle for ten minutes of it — `kraken_backpressure_probe.py`'s three-phase
// arrangement, with the phase lengths DERIVED from each venue's own cadence
// rather than fixed, because 639 s of baseline at Binance and 16 s at Anvil are
// the same experiment and a constant here would run two different ones.
struct Setup {
    double from_s = 0.0;
    std::size_t real_arrivals = 0;
};

Setup prepare(Arrivals& a, double extend_to, double from_s) {
    Setup w;
    w.real_arrivals = a.t.size();
    const double latch_s = kBaselineSamples * a.median_ms / 1000.0;
    extend(a, extend_to > 0.0 ? extend_to : latch_s + 600.0);
    w.from_s = from_s >= 0.0 ? from_s : baseline_latch_s(a) + a.median_ms / 1000.0;
    return w;
}

// ---------------------------------------------------------------------------
// The pin
// ---------------------------------------------------------------------------
//
// ADD ROWS, NEVER REGENERATE. Same rule and same reason as every other pin table
// in this repository. A verdict moving here means the estimator's arithmetic
// changed or a trace was re-captured, and both want a human.
struct Expect {
    const char* file;
    const char* socket_call;   // what the whole-stream backlog must produce
    const char* feed_call;     // what the book-only backlog must produce
    const char* note;
};

constexpr Expect kExpect[] = {
    {"binance_atomeur_d100ms_liveness_20260826.ndjson", "TRACKS", "BLIND",
     "Binance: the ping is emitted BELOW the subscription, so a feed-only "
     "backlog is a real state here and the meter cannot see it"},
    {"anvil_101_baseline.ndjson", "TRACKS", "BLIND",
     "Anvil CONTROL: identical arithmetic, and the feed-only column is a "
     "FICTION -- `summary` comes from the subsystem that publishes the book, so "
     "there is no way for one to be late and the other current"},
    {"kraken_btcusd_d25_20260816.ndjson", "TRACKS", "BLIND",
     "Kraken CONTROL: same, on the 1 Hz heartbeat"},
};

int check(const std::vector<std::string>& paths) {
    // Long enough to clear the 32-interval baseline at every venue's cadence.
    constexpr double kFactor = 0.25;
    int failures = 0;
    for (const std::string& path : paths) {
        const std::string name = basename_of(path);
        const Expect* want = nullptr;
        for (const Expect& e : kExpect) {
            if (name == e.file) { want = &e; }
        }
        if (want == nullptr) {
            std::printf("  [FAIL] %s: no pinned age-probe verdict on record. Add a "
                        "row rather than dropping the trace.\n",
                        name.c_str());
            ++failures;
            continue;
        }
        Arrivals a = read_arrivals(path);
        const Setup w = prepare(a, -1.0, -1.0);
        const Verdict s = run_socket(a, w.from_s, kFactor);
        const Verdict f = run_feed(a, w.from_s, kFactor);
        const bool ok = std::string(call_of(s, kFactor)) == want->socket_call &&
                        std::string(call_of(f, kFactor)) == want->feed_call;
        if (!ok) { ++failures; }
        std::printf("  [%s] %-52s socket %-10s feed %-10s  %s\n", ok ? " ok " : "FAIL",
                    name.c_str(), call_of(s, kFactor), call_of(f, kFactor), want->note);
        if (!ok) {
            std::printf("           pinned   socket %s  feed %s\n", want->socket_call,
                        want->feed_call);
        }
    }
    std::printf("\n  %zu trace(s) checked against the pinned age-probe verdicts: %s\n",
                paths.size(), failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}

int report(const std::string& path, double from_s, double factor, double extend_to) {
    Arrivals a = read_arrivals(path);
    if (a.t.size() < 2) {
        std::printf("%s: fewer than two liveness arrivals — nothing to measure\n",
                    path.c_str());
        return 1;
    }
    const Setup w = prepare(a, extend_to, from_s);
    const std::size_t real = w.real_arrivals;
    from_s = w.from_s;

    std::printf("\ndc_age_probe — %s (%s)\n", basename_of(path).c_str(),
                dc::harness::venue_traits(a.venue).name.data());
    std::printf("  liveness arrivals: %zu real", real);
    if (a.t.size() > real) {
        std::printf(" + %zu synthesised at the measured %.1f ms median (no capture "
                    "reaches the baseline at this cadence)",
                    a.t.size() - real, a.median_ms);
    }
    std::printf("\n");
    std::printf("  baseline needs %zu arrivals = %.0f s of wall clock at this "
                "cadence\n",
                kBaselineSamples + 1, kBaselineSamples * a.median_ms / 1000.0);

    // THE READING BEFORE ANY THROTTLE. On every committed Binance file this is
    // where the run ends, because the meter has nothing to say yet.
    const Verdict none = run_feed(a, 0.0, 1.0);
    if (!none.calibrated) {
        std::printf("  the meter reads `-` for this whole trace, throttle or no "
                    "throttle: %zu arrivals against the %zu the baseline needs\n",
                    a.t.size(), kBaselineSamples + 1);
    }

    std::printf("  throttle: %.0f%% of the broadcast rate, from t+%.0f s\n\n",
                factor * 100.0, from_s);
    const bool feed_physical = a.venue == dc::harness::Venue::Binance;
    print_verdict("socket backlog", run_socket(a, from_s, factor), true, factor);
    print_verdict("feed backlog", run_feed(a, from_s, factor), feed_physical, factor);
    std::printf("\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    bool do_check = false;
    // Negative means "derive it": the throttle starts when the baseline has
    // latched, and the series runs long enough for that plus a throttled phase.
    double from_s = -1.0;
    double factor = 0.25;
    double extend_to = -1.0;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--check") {
                do_check = true;
            } else if (arg == "--from" && i + 1 < argc) {
                from_s = std::atof(argv[++i]);
            } else if (arg == "--factor" && i + 1 < argc) {
                factor = std::atof(argv[++i]);
            } else if (arg == "--extend-to" && i + 1 < argc) {
                extend_to = std::atof(argv[++i]);
            } else if (!arg.empty() && arg[0] == '-') {
                std::fprintf(stderr,
                             "usage: dc_age_probe [--check] [--from S] [--factor F] "
                             "[--extend-to S] <trace.ndjson>...\n");
                return 2;
            } else {
                paths.push_back(arg);
            }
        }
        if (paths.empty()) {
            std::fprintf(stderr, "usage: dc_age_probe [--check] <trace.ndjson>...\n");
            return 2;
        }
        if (do_check) { return check(paths); }
        int rc = 0;
        for (const std::string& p : paths) {
            rc |= report(p, from_s, factor, extend_to);
        }
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "dc_age_probe: %s\n", e.what());
        return 1;
    }
}
