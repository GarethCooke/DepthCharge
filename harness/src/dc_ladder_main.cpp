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
//     --no-color         plain text, no ANSI
//     --ascii            no box-drawing / block glyphs
//     --quiet            report only, no ladder
//
// Exit 0 on a clean replay; 1 on bad usage or a malformed trace.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#include <depthcharge/anvil/anvil_adapter.hpp>

#include "dc_harness/console_ladder.hpp"
#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"

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
                 "          [--levels n] [--gap-ms ms] [--no-color] [--ascii] [--quiet]\n",
                 argv0);
    std::exit(1);
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
            args.speed = std::strtod(value("--speed needs a number").c_str(), nullptr);
            if (args.speed <= 0.0) { usage_exit(argv[0], "--speed must be > 0"); }
        } else if (a == "--at") {
            args.replay.max_frames =
                static_cast<std::size_t>(std::strtoull(value("--at needs a frame").c_str(),
                                                       nullptr, 10));
        } else if (a == "--levels") {
            args.style.levels =
                static_cast<std::size_t>(std::strtoull(value("--levels needs a count").c_str(),
                                                       nullptr, 10));
        } else if (a == "--gap-ms") {
            args.replay.disconnect_gap_ms =
                std::strtod(value("--gap-ms needs a number").c_str(), nullptr);
            if (args.replay.disconnect_gap_ms <= 0.0) {
                usage_exit(argv[0], "--gap-ms must be > 0");
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

void print_report(const Args& args, const ReplayResult& r) {
    const auto& a = r.adapter;
    std::printf("dc_ladder — DepthCharge M1 replay\n\n");
    std::printf("trace     : %s\n", args.path.c_str());
    std::printf("metadata  : ticker=%lld  mode=%s  captured_at=%s\n",
                static_cast<long long>(r.meta.ticker),
                r.meta.capture_mode.empty() ? "-" : r.meta.capture_mode.c_str(),
                r.meta.captured_at.c_str());
    std::printf("frames    : %zu over %.1f s\n", r.frames, r.span_seconds());
    std::printf("adapter   : events=%llu  snapshot=%llu book=%llu trade=%llu  "
                "summary_ignored=%llu\n",
                static_cast<unsigned long long>(a.events_out),
                static_cast<unsigned long long>(a.snapshot_frames),
                static_cast<unsigned long long>(a.book_frames),
                static_cast<unsigned long long>(a.trade_frames),
                static_cast<unsigned long long>(a.summary_ignored));
    std::printf("            parse_errors=%llu price_errors=%llu other_ticker=%llu "
                "unknown_kind=%llu truncated=%llu\n",
                static_cast<unsigned long long>(a.parse_errors),
                static_cast<unsigned long long>(a.price_errors),
                static_cast<unsigned long long>(a.other_ticker),
                static_cast<unsigned long long>(a.unknown_kind),
                static_cast<unsigned long long>(a.truncated_frames));
    std::printf("            wire seq went backwards %llu times "
                "(diagnostic only — Seq is synthesised)\n",
                static_cast<unsigned long long>(a.wire_seq_backward));
    std::printf("book      : snapshots_adopted=%llu trades=%llu gaps=%llu "
                "deltas_rejected=%llu\n",
                static_cast<unsigned long long>(r.book.snapshots_adopted),
                static_cast<unsigned long long>(r.book.trades_applied),
                static_cast<unsigned long long>(r.book.gaps),
                static_cast<unsigned long long>(r.book.deltas_rejected));
    std::printf("watchdog  : %.0f ms  ->  %zu stale episode(s)\n",
                args.replay.disconnect_gap_ms, r.episodes.size());
    for (const StaleEpisode& ep : r.episodes) {
        std::printf("            after frame %zu: %.0f ms silence; grey for %.0f ms; ",
                    ep.frame_before, ep.observed_gap_ms, ep.stale_ms);
        if (ep.cleared) {
            std::printf("cleared by the snapshot in frame %zu\n", ep.cleared_frame);
        } else {
            std::printf("never cleared (still stale at end of trace)\n");
        }
    }
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
        const depthcharge::SymbolSpec symbol{
            static_cast<std::uint32_t>(reader.meta().ticker >= 0 ? reader.meta().ticker
                                                                 : depthcharge::anvil::
                                                                       kAnvilTicker101.id),
            depthcharge::anvil::kAnvilTicker101.price_decimals,
            depthcharge::anvil::kAnvilTicker101.qty_step};

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
