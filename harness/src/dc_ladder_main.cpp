// dc_ladder — replay a captured trace through the engine and draw the ladder.
//
// M1's end-to-end proof, and the first thing in this repo that looks like the
// product: trace -> Anvil adapter -> phase-1 book -> DisplaySnapshot -> pixels
// (console ones). The default run is non-interactive and is what ctest executes;
// --follow paces the replay by the capture clock so a human can watch the
// reconnect trace go grey and come back.
//
//   usage: dc_ladder <trace.ndjson> [options]
//
//     --follow           redraw as the trace replays, paced by rx_ns
//     --speed <x>        time multiplier for --follow (default 1.0)
//     --at <frame>       stop after frame N (0 = whole trace)
//     --levels <n>       levels per side, max 27 (default 12)
//     --gap-ms <ms>      RX-watchdog threshold for Gap{Disconnect} (default 1000)
//     --end-silence-ms <ms>  silence after the last frame (default 0 = unknown);
//                        a file has no "now", so trailing silence is invisible
//                        unless the caller says how long it lasted
//     --no-color         plain text, no ANSI
//     --ascii            no box-drawing / block glyphs
//     --quiet            report only, no ladder
//
// Exit 0 on a clean replay; 1 on bad usage or a malformed trace.
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>

#include <depthcharge/anvil/anvil_adapter.hpp>
#include <depthcharge/window.hpp>

#include <depthcharge/age_estimator.hpp>
#include "dc_harness/console_ladder.hpp"
#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"
#include "dc_harness/venue.hpp"

namespace {

using dc::harness::LadderStyle;
using dc::harness::ReplayOptions;
using dc::harness::ReplayResult;
using dc::harness::ReplayStep;
using dc::harness::StaleEpisode;

struct Args {
    std::string path;
    LadderStyle style{};
    ReplayOptions replay{};
    bool follow = false;
    bool quiet = false;
    double speed = 1.0;
};

[[noreturn]] void usage_exit(const char* argv0, const char* problem) {
    if (problem != nullptr) { std::fprintf(stderr, "dc_ladder: %s\n", problem); }
    std::fprintf(stderr,
                 "usage: %s <trace.ndjson> [--follow] [--speed x] [--at N]\n"
                 "          [--levels n] [--gap-ms ms] [--end-silence-ms ms]\n"
                 "          [--window top|largest|thinned]\n"
                 "          [--no-color] [--ascii] [--quiet]\n",
                 argv0);
    std::exit(1);
}

// Parse a whole argument, or exit with usage.
//
// std::from_chars rather than strtoull/strtod: the old form reported nothing, so
// `--at nonsense` silently replayed the entire trace and `--at -1` wrapped
// through to SIZE_MAX and did the same. Requiring the whole token to be consumed
// also rejects `--at 5x`.
//
// The narrowing this brings is deliberate and small: from_chars does not accept
// a leading '+', which strtod/strtoull did. Nobody writes `--speed +2.5`, and
// keeping it would mean writing code to preserve a syntax with no users.
template <typename T>
T parse_arg(const char* argv0, std::string_view text, const char* what) {
    T value{};
    const char* const last = text.data() + text.size();
    const auto res = std::from_chars(text.data(), last, value);
    if (res.ec != std::errc{} || res.ptr != last) { usage_exit(argv0, what); }
    if constexpr (std::is_floating_point_v<T>) {
        // from_chars accepts "nan" and "inf" exactly as strtod did, and since
        // `nan <= 0.0` is false they sail straight through the range guards
        // below. This is the check that actually closes the class of input the
        // switch to from_chars was made for.
        if (!std::isfinite(value)) { usage_exit(argv0, what); }
    }
    return value;
}

bool parse_args(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto value = [&](const char* what) -> std::string {
            if (i + 1 >= argc) { usage_exit(argv[0], what); }
            return argv[++i];
        };
        if (a == "--follow") {
            args.follow = true;
        } else if (a == "--quiet") {
            args.quiet = true;
        } else if (a == "--no-color") {
            args.style.color = false;
        } else if (a == "--ascii") {
            args.style.unicode = false;
        } else if (a == "--speed") {
            args.speed = parse_arg<double>(argv[0], value("--speed needs a number"),
                                           "--speed needs a finite number");
            if (args.speed <= 0.0) { usage_exit(argv[0], "--speed must be > 0"); }
        } else if (a == "--at") {
            args.replay.max_frames = parse_arg<std::size_t>(
                argv[0], value("--at needs a frame"), "--at needs a whole frame number");
        } else if (a == "--window") {
            // Refused rather than defaulted on an unknown name: two runs that
            // differ only in a misspelling must not produce identical output.
            if (!depthcharge::window::policy_from_name(value("--window needs a policy"),
                                                       args.replay.window_policy)) {
                usage_exit(argv[0], "--window must be top, largest or thinned");
            }
        } else if (a == "--levels") {
            args.style.levels = parse_arg<std::size_t>(
                argv[0], value("--levels needs a count"), "--levels needs a whole count");
        } else if (a == "--gap-ms") {
            args.replay.disconnect_gap_ms =
                parse_arg<double>(argv[0], value("--gap-ms needs a number"),
                                  "--gap-ms needs a finite number");
            if (args.replay.disconnect_gap_ms <= 0.0) {
                usage_exit(argv[0], "--gap-ms must be > 0");
            }
        } else if (a == "--end-silence-ms") {
            args.replay.end_of_trace_silence_ms =
                parse_arg<double>(argv[0], value("--end-silence-ms needs a number"),
                                  "--end-silence-ms needs a finite number");
            if (args.replay.end_of_trace_silence_ms < 0.0) {
                usage_exit(argv[0], "--end-silence-ms must be >= 0");
            }
        } else if (!a.empty() && a[0] == '-') {
            usage_exit(argv[0], "unknown option");
        } else if (args.path.empty()) {
            args.path = a;
        } else {
            usage_exit(argv[0], "more than one trace given");
        }
    }
    return !args.path.empty();
}

// std::uint64_t is `unsigned long` on Ubuntu and `unsigned long long` on
// MinGW-w64, so %llu needs the cast on one and not the other — and
// -Werror=format makes the mismatch fatal on the first. One helper instead of
// thirteen call-site casts. Deliberately not std::format: it pulls in the whole
// std::locale facet suite (+90 KiB stripped, +3.4 s on this TU) and allocates
// per call, an idiom that must not get anywhere near engine/ or firmware/.
constexpr unsigned long long ull(std::uint64_t v) noexcept { return v; }

// The adapter block, per venue. THE REPORT HAD TO GROW A SECOND HALF THE MOMENT
// THE DRIVER GREW A SECOND ADAPTER, and printing Anvil's counters for a Kraken
// run would not have failed — it would have printed a page of zeros for a replay
// that worked perfectly, which is the same class of quiet wrongness as a golden
// produced by the wrong parser. `r.decoder` is printed at the top so a reader
// never has to infer which of the two blocks they are looking at.
void print_anvil_adapter(const ReplayResult& r) {
    const auto& a = r.adapter;
    std::printf("adapter   : events=%llu  snapshot=%llu book=%llu trade=%llu  "
                "summary_ignored=%llu\n",
                ull(a.events_out),
                ull(a.snapshot_frames),
                ull(a.book_frames),
                ull(a.trade_frames),
                ull(a.summary_ignored));
    std::printf("            parse_errors=%llu price_errors=%llu other_ticker=%llu "
                "unknown_kind=%llu truncated=%llu\n",
                ull(a.parse_errors),
                ull(a.price_errors),
                ull(a.other_ticker),
                ull(a.unknown_kind),
                ull(a.truncated_frames));
    std::printf("            wire seq went backwards %llu times "
                "(diagnostic only — Seq is synthesised)\n",
                ull(a.wire_seq_backward));
}

// Binance's adapter block (M5 stage B1). A THIRD BLOCK for the same reason there
// was a second: printing another venue's counters would not fail, it would print
// a page of zeros for a replay that worked.
//
// Three things here are deliverables rather than curiosities:
//
//   * THE SEED — its bracket, the events buffered while the fetch was in flight,
//     and how many of them the snapshot already contained. A REST round trip is
//     ~1.0-1.5 s, so a seed adopted without buffering is a book missing a second
//     and a half of amendments.
//   * `absent` REMOVALS, which are documented venue behaviour and CONSTANT: the
//     venue's book is deeper than anything it sends, so removals for levels
//     outside the seeded window arrive continuously. A zero here would be the
//     surprising reading.
//   * THE SEEDED-WINDOW EDGE, reported and deliberately NOT acted on. When the
//     market walks far enough that the rendered window approaches the edge of
//     what the seed contained, it shows as the book's low-water depth falling
//     and as evictions. The re-snapshot schedule that would answer it is B2's
//     and needs a measured basis, so nothing branches on these — the same
//     discipline stage C used for the window policies: record per policy, solve
//     nowhere.
void print_binance_adapter(const ReplayResult& r) {
    const auto& b = r.binance;
    std::printf("adapter   : events=%llu  diff=%llu partial=%llu rest=%llu(+%llu no-body) "
                "ack=%llu unknown=%llu\n",
                ull(b.events_out), ull(b.diff_frames), ull(b.partial_frames),
                ull(b.rest_snapshots), ull(b.rest_no_body), ull(b.acks),
                ull(b.unknown_kind));
    std::printf("            parse_errors=%llu price_errors=%llu qty_errors=%llu "
                "other_symbol=%llu overflow=%llu\n",
                ull(b.parse_errors), ull(b.price_errors), ull(b.qty_errors),
                ull(b.other_symbol), ull(b.overflow_frames));
    std::printf("seed      : bracket ok=%llu failed=%llu   buffered=%llu "
                "(dropped by seed=%llu, overflow=%llu)   reseeds=%llu\n",
                ull(b.seed_bracket_ok), ull(b.seed_bracket_failed),
                ull(b.buffered_events), ull(b.buffered_dropped_by_seed),
                ull(b.buffer_overflows), ull(b.reseeds_requested));
    std::printf("            U/u seq breaks=%llu -> Gap{SeqGap}. A TRANSPORT CHECK: it "
                "caught 2,204 missed updates at a reconnect and 0 of 3 book mutants\n",
                ull(b.seq_breaks));
    std::printf("levels    : applied=%llu removed=%llu unchanged=%llu   absent "
                "removals=%llu (NORMAL: the venue's book is deeper than it sends)\n",
                ull(b.levels_applied), ull(b.levels_removed), ull(b.levels_unchanged),
                ull(b.levels_absent_removals));
    std::printf("edge      : book low-water bid=%u ask=%u of %u held   evicted=%llu   "
                "window exits=%llu entries=%llu   outside emit=%llu\n",
                b.min_bid_levels == 0xFFFFFFFFu ? 0u : b.min_bid_levels,
                b.min_ask_levels == 0xFFFFFFFFu ? 0u : b.min_ask_levels,
                static_cast<unsigned>(depthcharge::binance::kBinanceMaxFrameLevels),
                ull(b.levels_evicted), ull(b.window_exits), ull(b.window_entries),
                ull(b.levels_outside_emit));
    // WHAT THE ORACLE REACHES, on the line rather than in a comment. `@depth20`
    // validates the top 20 a side while the panel draws 25 — the same shape of
    // gap as Kraken's CRC-10-of-25, and smaller. `dc_binance_oracle` performs
    // the comparison; this run does not.
    std::printf("            oracle: @depth20 covers the top 20 of the 25 rows drawn; "
                "grading is dc_binance_oracle's, not this run's\n");
}

// THE WINDOW (M4 stage C). Printed for every venue and every policy, because
// the question stage D has to answer at the panel — is this window worth looking
// at — is not answerable from the ladder alone.
//
// `dropped` is the number to read first: how many levels the book held and the
// window gave no row to. It is ZERO on every committed Kraken slice at the
// shipped depth of 25, because 25 levels fit 27 rows — and where it is zero all
// three policies are the same window, so no amount of staring at the panel will
// separate them.
//
// `validated` is B2's: how many rendered rows show a level the venue's own CRC32
// ever confirmed. At Kraken that is the top 10 a side and no more, so a window
// that reaches below the touch is showing rows nobody checked. Reported, never
// branched on.
void print_window(const ReplayResult& r) {
    const auto& w = r.window;
    const auto name = depthcharge::window::policy_name(w.policy);
    std::printf("window    : policy=%.*s  rows filled=%llu unknown=%llu  "
                "dropped=%llu (in %llu frame(s))\n",
                static_cast<int>(name.size()), name.data(),
                ull(w.rows_filled), ull(w.rows_unknown),
                ull(w.levels_dropped), ull(w.frames_with_drops));
    std::printf("            final bid rows=%u/%u span=%lld tk   "
                "final ask rows=%u/%u span=%lld tk   worst span=%lld tk\n",
                w.final_bid.rows_filled, w.final_bid.levels_offered,
                static_cast<long long>(w.final_bid.tick_span),
                w.final_ask.rows_filled, w.final_ask.levels_offered,
                static_cast<long long>(w.final_ask.tick_span),
                static_cast<long long>(w.worst_tick_span));
    if (w.validated_depth == 0) {
        std::printf("            this venue publishes no checksum, so NO rendered row was "
                    "ever externally confirmed\n");
    } else {
        std::printf("            %llu of %llu rendered rows were within the venue's "
                    "checksum (top %u a side)\n",
                    ull(w.rows_validated), ull(w.rows_filled), w.validated_depth);
    }
}

void print_kraken_adapter(const ReplayResult& r) {
    const auto& k = r.kraken;
    // `unsub` is printed beside `ack` rather than folded into it: the two frames
    // are structurally identical on this wire and mean opposite things, and an
    // adapter that conflated them was the defect the resync capture found.
    std::printf("adapter   : events=%llu  snapshot=%llu update=%llu heartbeat=%llu "
                "status=%llu ack=%llu unsub=%llu\n",
                ull(k.events_out),
                ull(k.snapshot_frames),
                ull(k.update_frames),
                ull(k.heartbeats),
                ull(k.status_frames),
                ull(k.acks),
                ull(k.unsubscribe_acks));
    std::printf("            parse_errors=%llu price_errors=%llu qty_errors=%llu "
                "other_symbol=%llu unknown_kind=%llu\n",
                ull(k.parse_errors),
                ull(k.price_errors),
                ull(k.qty_errors),
                ull(k.other_symbol),
                ull(k.unknown_kind));
    // The truncation line. `evicted` counts levels Kraken never sent a removal
    // for and the client had to drop itself — stage 0 measured a non-truncating
    // client wrong in 1,077 of 1,537 messages at depth 25, so a zero here on a
    // busy BTC/USD slice means the rule is not running.
    std::printf("            levels applied=%llu removed=%llu evicted_by_truncation=%llu "
                "outside_depth=%llu unchanged=%llu\n",
                ull(k.levels_applied),
                ull(k.levels_removed),
                ull(k.levels_evicted),
                ull(k.levels_outside_depth),
                ull(k.levels_unchanged));
    // THE CHECKSUM LEDGER (B2). The three outcomes are printed together and
    // always, including the zeros, because "0 failed" and "the line is absent"
    // are the same picture otherwise — and `unverifiable` is the one that must
    // never be read as a pass: it means there was no baseline to compare
    // against, which is the whole of the mid-stream slice.
    //
    // The coverage note rides on the same line rather than in a footnote nobody
    // reads. At the shipped depth of 25 this number validates the top 10 levels
    // a side and says nothing about the other 15.
    std::printf("checksum  : seen=%llu  matched=%llu  FAILED=%llu  unverifiable=%llu "
                "(no baseline)  resyncs_requested=%llu\n",
                ull(k.checksums_seen),
                ull(k.checksums_matched),
                ull(k.checksums_failed),
                ull(k.checksums_unverifiable),
                ull(k.resyncs_requested));
    std::printf("            covers the top %u levels a side only — levels %u+ are "
                "rendered and NOT validated by it\n",
                depthcharge::kraken::kChecksumLevels,
                depthcharge::kraken::kChecksumLevels + 1);
    std::printf("            deltas dropped before baseline=%llu\n",
                ull(k.deltas_before_baseline));
}

void print_report(const Args& args, const ReplayResult& r) {
    std::printf("dc_ladder — DepthCharge replay\n\n");
    std::printf("trace     : %s\n", args.path.c_str());
    std::printf("decoder   : %s\n", r.decoder.c_str());
    // Both symbol-identified venues; Binance records no `depth` (its
    // subscription is named by `streams` in the header), so it prints -1 the way
    // a Kraken capture predating the field does.
    if (r.meta.venue == dc::harness::Venue::Kraken ||
        r.meta.venue == dc::harness::Venue::Binance) {
        std::printf("metadata  : symbol=%s  depth=%lld  captured_at=%s\n",
                    r.meta.symbol.c_str(), static_cast<long long>(r.meta.depth),
                    r.meta.captured_at.c_str());
    } else {
        std::printf("metadata  : ticker=%lld  mode=%s  captured_at=%s\n",
                    static_cast<long long>(r.meta.ticker),
                    r.meta.capture_mode.empty() ? "-" : r.meta.capture_mode.c_str(),
                    r.meta.captured_at.c_str());
    }
    std::printf("frames    : %zu over %.1f s\n", r.frames, r.span_seconds());
    // No `default:`, so a fourth venue is a -Wswitch error rather than a page of
    // another venue's zeros (M5 stage A, deliverable 3).
    switch (r.meta.venue) {
        case dc::harness::Venue::Kraken:  print_kraken_adapter(r); break;
        case dc::harness::Venue::Binance: print_binance_adapter(r); break;
        case dc::harness::Venue::Anvil:   print_anvil_adapter(r); break;
    }
    std::printf("book      : snapshots_adopted=%llu trades=%llu gaps=%llu  "
                "deltas applied=%llu removed=%llu absent=%llu\n",
                ull(r.book.snapshots_adopted),
                ull(r.book.trades_applied),
                ull(r.book.gaps),
                ull(r.book.deltas_applied),
                ull(r.book.deltas_removed),
                ull(r.book.deltas_absent));
    std::printf("watchdog  : %.0f ms  ->  %zu stale episode(s)\n",
                args.replay.disconnect_gap_ms > 0.0 ? args.replay.disconnect_gap_ms
                                                    : r.threshold_ms,
                r.episodes.size());
    for (const StaleEpisode& ep : r.episodes) {
        std::printf("            after frame %zu: %.0f ms silence", ep.frame_before,
                    ep.observed_gap_ms);
        if (ep.gap_events > 1) {
            std::printf(" (%zu watchdog firings, no resync between)", ep.gap_events);
        }
        std::printf("; grey for %.0f ms; ", ep.stale_ms);
        if (ep.cleared) {
            std::printf("cleared by the snapshot in frame %zu\n", ep.cleared_frame);
        } else {
            std::printf("never cleared (still stale at end of trace)\n");
        }
    }

    // THE AGE METER, ON ITS OWN LINE AND NOT NEAR THE WATCHDOG (M4 stage A2).
    // The two are computed from the same arrivals and mean opposite things: the
    // watchdog above says whether the panel greys, and this says how far behind
    // a panel that is NOT greying has fallen. Both numbers are printed with the
    // median they derive from, because every age here is `elapsed - n x median`
    // and a reader who cannot see the median cannot check the arithmetic.
    const depthcharge::AgeText worst_age(r.worst_age_ms);
    const depthcharge::AgeText final_age(r.final_snapshot.age_ms);
    const std::string_view signal = dc::harness::venue_traits(r.meta.venue).liveness_signal;
    print_window(r);
    std::printf("age       : worst %s   at end %s   (queuing lag, never a grey signal)\n",
                worst_age.buf,
                r.final_snapshot.has_age ? final_age.buf : "no reading yet");
    std::printf("            %zu x %.*s: baseline %.1f ms (this connection's own first "
                "window), rolling median %.1f ms\n",
                r.liveness_arrivals, static_cast<int>(signal.size()), signal.data(),
                r.age_baseline_ms, r.liveness_median_ms);
    std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) { usage_exit(argv[0], "no trace given"); }

    // The panel is 64 rows; the console preview keeps to the same budget.
    if (args.style.levels == 0 || args.style.levels > depthcharge::kDisplayLevels) {
        args.style.levels = depthcharge::kDisplayLevels;
    }

    try {
        dc::harness::TraceReader reader(args.path);
        const depthcharge::SymbolSpec symbol = dc::harness::symbol_for(reader.meta());

        // The header names the venue the trace declares, not a compiled-in
        // guess. Uppercased here rather than in venue.hpp because that table's
        // `name` is the METADATA TAG — the string a capture file is matched on —
        // and a table whose key is also a display string is one edit away from a
        // trace that no longer parses.
        std::string venue_label(dc::harness::venue_traits(reader.venue()).name);
        for (char& ch : venue_label) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        args.style.venue = venue_label;
        args.style.symbol = reader.meta().symbol;  // empty at Anvil => the ticker id

        dc::harness::ReplayObserver observer;
        std::int64_t last_rx = 0;
        bool have_last_rx = false;
        if (args.follow) {
            observer = [&](const ReplayStep& step,
                           const depthcharge::DisplaySnapshot& snap) -> bool {
                if (have_last_rx && step.rx_ns > last_rx) {
                    const double ms =
                        static_cast<double>(step.rx_ns - last_rx) / 1e6 / args.speed;
                    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
                }
                last_rx = step.rx_ns;
                have_last_rx = true;
                std::fputs(dc::harness::ladder_home(args.style).c_str(), stdout);
                std::fputs(dc::harness::render_ladder(snap, args.style).c_str(), stdout);
                std::fflush(stdout);
                return true;
            };
        }

        const ReplayResult result =
            dc::harness::run_replay(reader, symbol, args.replay, observer);

        if (args.follow) { std::printf("\n"); }
        print_report(args, result);

        if (!args.quiet) {
            // A trace that went stale prints both ladders: the grey one as it
            // was at the watchdog, and the recovered one. Seeing them next to
            // each other is the point of invariant #5 — and it is why the
            // reconnect trace is committed.
            if (result.saw_stale) {
                std::printf("--- at the disconnect ---\n");
                std::fputs(dc::harness::render_ladder(result.first_stale_snapshot, args.style)
                               .c_str(),
                           stdout);
                std::printf("\n--- end of trace ---\n");
            }
            std::fputs(dc::harness::render_ladder(result.final_snapshot, args.style).c_str(),
                       stdout);
        }
        std::printf("\nOK\n");
    } catch (const dc::harness::TraceError& e) {
        std::fprintf(stderr, "dc_ladder: %s: %s\n", args.path.c_str(), e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "dc_ladder: %s\n", e.what());
        return 1;
    }
    return 0;
}
