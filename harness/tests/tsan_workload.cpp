// tsan_workload.cpp — the SnapshotChannel under ThreadSanitizer.
//
// M3 stage A asks for "a committed clean ThreadSanitizer report, the way Anvil's
// Stage 0 proved its concurrent paths". This is that workload, and it is a
// standalone `main` rather than a doctest case for the same reason Anvil's is
// Crow-free: the report has to be unambiguous about what was instrumented. The
// only thing racing here is one feed thread and one render thread across
// depthcharge::SnapshotChannel — no test framework, no JSON parser, no trace
// reader — so a diagnostic can only be about the channel.
//
// It is also built without the sanitiser by the ordinary host build and run as a
// short ctest, so it cannot rot on the machines where TSan is unavailable —
// which on this project is the primary one, since ThreadSanitizer needs Linux
// and the desk is Windows/MinGW. See harness/tsan.sh.
//
// The correctness rules are the ones test_snapshot_channel.cpp asserts, out of
// the same header: TSan proves there was no race, and the checks prove the
// race-free hand-off also handed over the right thing. Neither is enough alone —
// a channel that published nothing at all would be perfectly clean.
//
//   usage: dc_tsan_workload [seconds]        default 1
//   exit:  0 clean · 1 a rule was broken · 2 bad usage
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/snapshot_channel.hpp>

#include "channel_stress.hpp"

using depthcharge::DisplaySnapshot;
using depthcharge::SnapshotChannel;
using dc::testing::channel::ConsumerReport;
using dc::testing::channel::consume_checked;
using dc::testing::channel::publish_stamped;

namespace {

using ull = unsigned long long;

struct Phase {
    const char* name;
    bool reader_dawdles;  // the microcontroller case: render task busy elsewhere
};

struct PhaseResult {
    std::uint64_t published = 0;
    ConsumerReport report;
};

PhaseResult run_phase(const Phase& phase, std::chrono::milliseconds duration) {
    SnapshotChannel channel;

    // One variable per thread, assembled into the result after both joins, so
    // that "who writes what" is visible rather than argued: the feed thread owns
    // `published`, the render thread owns `report`, and main owns neither until
    // they are gone.
    std::uint64_t published = 0;
    ConsumerReport report;

    // The only cross-thread words in this program besides the channel itself.
    // Atomic for exactly the reason the channel has three slots: a plain bool
    // here would be a data race, and it would be *this file's* race showing up
    // in a report that is supposed to be about engine/.
    std::atomic<bool> stop_feed{false};
    std::atomic<bool> feed_done{false};

    std::thread feed([&] {
        DisplaySnapshot scratch{};
        std::uint32_t version = 0;
        while (!stop_feed.load(std::memory_order_relaxed)) {
            publish_stamped(channel, scratch, ++version);
        }
        published = version;
    });

    std::thread render([&] {
        DisplaySnapshot out{};
        std::uint32_t idle = 0;
        while (!feed_done.load(std::memory_order_acquire)) {
            if (consume_checked(channel, out, report)) {
                if (phase.reader_dawdles) {
                    // Long enough that the writer laps the buffers many times
                    // over — the interleaving in which a two-slot design would
                    // be overwriting the slot currently being read.
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            } else if (++idle % 4096 == 0) {
                std::this_thread::yield();
            }
        }
        // The feed has stopped, so this drains rather than chases: the last
        // frame published in the phase is checked like every other one.
        while (consume_checked(channel, out, report)) {}
    });

    std::this_thread::sleep_for(duration);
    stop_feed.store(true, std::memory_order_relaxed);
    feed.join();
    feed_done.store(true, std::memory_order_release);
    render.join();

    return PhaseResult{published, report};
}

bool report_phase(const Phase& phase, const PhaseResult& r) {
    std::printf("phase %-12s published=%-10llu delivered=%-9llu empty_polls=%llu\n", phase.name,
                static_cast<ull>(r.published), static_cast<ull>(r.report.consumed),
                static_cast<ull>(r.report.empty_polls));
    std::printf("                   versions delivered %llu .. %llu\n",
                static_cast<ull>(r.report.first_version),
                static_cast<ull>(r.report.last_version));

    bool ok = true;
    if (r.report.torn_where != nullptr) {
        std::printf("  FAIL torn frame: field '%s' at version %llu\n", r.report.torn_where,
                    static_cast<ull>(r.report.torn_version));
        ok = false;
    }
    if (r.report.regressed) {
        std::printf("  FAIL delivered version went backwards: %llu -> %llu\n",
                    static_cast<ull>(r.report.regressed_from),
                    static_cast<ull>(r.report.regressed_to));
        ok = false;
    }
    // A phase that delivered nothing would be clean under TSan and worthless as
    // evidence, so an idle run fails here rather than passing quietly.
    if (r.published == 0 || r.report.consumed == 0) {
        std::printf("  FAIL phase did no work — nothing was raced\n");
        ok = false;
    }
    if (r.report.last_version > r.published) {
        std::printf("  FAIL delivered version %llu was never published (max %llu)\n",
                    static_cast<ull>(r.report.last_version), static_cast<ull>(r.published));
        ok = false;
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    double seconds = 1.0;
    if (argc > 2) {
        std::fprintf(stderr, "usage: %s [seconds]\n", argv[0]);
        return 2;
    }
    if (argc == 2) {
        char* end = nullptr;
        seconds = std::strtod(argv[1], &end);
        if (end == argv[1] || *end != '\0' || !(seconds > 0.0) || seconds > 3600.0) {
            std::fprintf(stderr, "%s: bad duration '%s'\n", argv[0], argv[1]);
            return 2;
        }
    }

    const Phase phases[] = {
        {"tight", false},       // both threads flat out: maximum publish rate
        {"slow-reader", true},  // render task parked: the writer laps it
    };

    const auto phase_count = static_cast<long long>(sizeof phases / sizeof phases[0]);
    auto per_phase_ms = static_cast<long long>(seconds * 1000.0) / phase_count;
    if (per_phase_ms < 1) { per_phase_ms = 1; }
    const auto per_phase = std::chrono::milliseconds(per_phase_ms);

    std::printf("dc_tsan_workload: DisplaySnapshot=%llu B  SnapshotChannel=%llu B  %lld ms/phase\n",
                static_cast<ull>(sizeof(DisplaySnapshot)), static_cast<ull>(sizeof(SnapshotChannel)),
                per_phase_ms);

    bool ok = true;
    for (const Phase& phase : phases) {
        ok = report_phase(phase, run_phase(phase, per_phase)) && ok;
    }

    std::printf("dc_tsan_workload: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
