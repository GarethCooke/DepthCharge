// test_snapshot_channel.cpp — M3 stage A: the feed -> render hand-off, proven.
//
// M1's channel was a single slot behind an honest disclaimer ("single-threaded
// use only — not a synchronisation primitive yet"). M3 makes it the real
// cross-core mailbox, and this file is the evidence for the two invariants that
// swap is answerable for:
//
//   #4  the feed task is never blocked by the render task. A parked reader must
//       not stop the writer, and must not make it queue: on waking, the reader
//       gets the newest frame, not the oldest.
//   #8  one writer, one reader, meeting only here — and never inside the same
//       DisplaySnapshot at the same time. That is what "no torn read" means, and
//       what the two-thread test below is looking for.
//
// The tearing check is structural rather than statistical: channel_stress.hpp
// stamps every field of the frame from its own version, so a delivered frame is
// either whole or provably spliced from two publishes, and the checker names the
// field where it split. A test that only compared versions would pass on a
// channel that tore every single frame.
//
// Worker threads record into a plain struct and the assertions run after the
// join — no doctest macro is ever called off the main thread, so the suite does
// not depend on the framework's thread-safety.
//
// This is the host half of the proof. The other half is the ThreadSanitizer run
// over the same helpers (harness/tests/tsan_workload.cpp, harness/tsan.sh):
// these tests can show that no torn frame *was* observed, and only TSan can show
// there was no race to observe.
#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/snapshot_channel.hpp>

#include "channel_stress.hpp"

using depthcharge::DisplaySnapshot;
using depthcharge::SnapshotChannel;
using dc::testing::channel::ConsumerReport;
using dc::testing::channel::consume_checked;
using dc::testing::channel::publish_stamped;
using dc::testing::channel::stamp;
using dc::testing::channel::torn_field;

TEST_CASE("a fresh channel reports nothing new and does not invent a frame") {
    SnapshotChannel ch;
    DisplaySnapshot out{};
    out.version = 12345;  // poisoned, so a spurious write would be visible

    CHECK_FALSE(ch.consume(out));
    CHECK(out.version == 12345);  // consume() left the caller's frame alone
    CHECK(ch.published_version() == 0);
    CHECK(ch.consumed_version() == 0);
}

TEST_CASE("the reader is handed the latest frame, never a queue of old ones") {
    SnapshotChannel ch;
    DisplaySnapshot scratch{}, out{};

    for (std::uint32_t v = 1; v <= 50; ++v) { publish_stamped(ch, scratch, v); }

    REQUIRE(ch.consume(out));
    CHECK(out.version == 50);
    CHECK(torn_field(out) == nullptr);
    CHECK(ch.published_version() == 50);
    CHECK(ch.consumed_version() == 50);

    // Nothing queued behind it: one publish, one frame available, and no more.
    CHECK_FALSE(ch.consume(out));
}

TEST_CASE("every publish/consume interleaving delivers a whole, current frame") {
    // Deterministic pseudo-random interleaving rather than the two orders a
    // hand-written test would think of. The channel's slot bookkeeping is a
    // three-way rotation driven by the *pattern* of calls, so the bug class this
    // is hunting — an index adopted from the wrong side — needs the pattern
    // varied, not the timing. No threads, so a failure here is reproducible.
    SnapshotChannel ch;
    DisplaySnapshot scratch{}, out{};
    ConsumerReport report;

    std::uint32_t published = 0;
    std::uint32_t rng = 0x2545f491u;
    for (int step = 0; step < 20000; ++step) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;

        if ((rng & 1u) != 0) {
            publish_stamped(ch, scratch, ++published);
        } else if (consume_checked(ch, out, report)) {
            // Latest-wins is exact, not approximate: single-threaded, a
            // successful consume must deliver the most recent publish.
            REQUIRE(out.version == published);
        }
    }

    CHECK(report.torn_where == nullptr);
    CHECK_FALSE(report.regressed);
    CHECK(report.consumed > 1000);      // the interleaving really did both things
    CHECK(report.empty_polls > 1000);
}

TEST_CASE("two threads: no frame is ever torn and delivered versions only move forward") {
    constexpr std::uint32_t kFrames = 100000;

    SnapshotChannel ch;
    ConsumerReport report;

    std::thread producer([&] {
        DisplaySnapshot scratch{};
        for (std::uint32_t v = 1; v <= kFrames; ++v) { publish_stamped(ch, scratch, v); }
    });

    std::thread consumer([&] {
        DisplaySnapshot out{};
        // Bounded rather than "until done": the last publish stays available
        // until it is taken, so a correct channel always reaches kFrames. The
        // deadline is there so a broken one fails the test instead of hanging
        // the suite — which means it has to be checked on every iteration, not
        // just the ones that found nothing. A channel that kept handing back the
        // same stale version forever would never take the empty branch at all.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        std::uint32_t since_clock_check = 0;
        while (report.last_version < kFrames) {
            consume_checked(ch, out, report);
            // The clock is far more expensive than the consume; sampling it
            // keeps the deadline a backstop rather than the thing being timed.
            if (++since_clock_check >= 4096) {
                since_clock_check = 0;
                if (std::chrono::steady_clock::now() > deadline) {
                    report.timed_out = true;
                    return;
                }
            }
        }
    });

    // Both joins returning is itself the invariant-#4 result: a writer that
    // could be held up by the reader would still be inside publish() here, and
    // the ctest timeout — not an assertion — is what would report it.
    producer.join();
    consumer.join();

    if (report.torn_where != nullptr) {
        // std::string, not the raw pointer: doctest stringifies a const char* as
        // an address, which is the least useful thing it could say here.
        MESSAGE("torn field '" << std::string(report.torn_where) << "' at version "
                               << report.torn_version);
    }
    if (report.regressed) {
        MESSAGE("version went backwards: " << report.regressed_from << " -> "
                                           << report.regressed_to);
    }

    CHECK_FALSE(report.timed_out);
    CHECK(report.torn_where == nullptr);
    CHECK_FALSE(report.regressed);

    // The reader is allowed to miss frames — that is invariant #4 working — but
    // it must have seen the first and last, and more than a handful in between,
    // or the run did not actually race and proves nothing.
    CHECK(report.last_version == kFrames);
    CHECK(report.consumed > 100);
    CHECK(report.consumed <= kFrames);
}

TEST_CASE("a parked reader neither blocks the writer nor makes it queue (invariant #4)") {
    // The microcontroller's common case: the render task is busy pushing pixels
    // for far longer than the feed task takes to fold a frame.
    constexpr std::uint32_t kFrames = 20000;

    SnapshotChannel ch;
    std::thread writer([&] {
        DisplaySnapshot scratch{};
        for (std::uint32_t v = 1; v <= kFrames; ++v) { publish_stamped(ch, scratch, v); }
    });

    // The writer runs every publish to completion with no reader in sight. A
    // design the consumer could hold up would sit in this join until the ctest
    // timeout, which is the honest form of the check — a stopwatch assertion
    // would only measure the build machine.
    writer.join();

    DisplaySnapshot out{};
    REQUIRE(ch.consume(out));
    CHECK(out.version == kFrames);          // newest, not oldest: no queue behind it
    CHECK(torn_field(out) == nullptr);
    CHECK_FALSE(ch.consume(out));           // and exactly one frame was waiting
    CHECK(ch.consumed_version() == kFrames);
}

TEST_CASE("the version stamps report what actually crossed, not what was attempted") {
    SnapshotChannel ch;
    DisplaySnapshot scratch{}, out{};

    publish_stamped(ch, scratch, 7);
    CHECK(ch.published_version() == 7);
    CHECK(ch.consumed_version() == 0);   // published is not delivered

    REQUIRE(ch.consume(out));
    CHECK(ch.consumed_version() == 7);

    publish_stamped(ch, scratch, 8);
    publish_stamped(ch, scratch, 9);
    CHECK(ch.published_version() == 9);
    CHECK(ch.consumed_version() == 7);   // 8 was overwritten, never delivered

    REQUIRE(ch.consume(out));
    CHECK(out.version == 9);
    CHECK(ch.consumed_version() == 9);
}

TEST_CASE("the stress helper can actually see a torn frame") {
    // Mutation guard. Every assertion above rests on torn_field() being able to
    // fail; if the stamping ever degenerated (all-zero fields, say) the whole
    // file would pass vacuously. So: splice two versions by hand and require the
    // checker to catch it, in the tail of the struct rather than the head.
    DisplaySnapshot a{}, b{};
    stamp(a, 1000);
    stamp(b, 1001);
    REQUIRE(torn_field(a) == nullptr);
    REQUIRE(torn_field(b) == nullptr);

    DisplaySnapshot spliced = a;
    spliced.trades[depthcharge::kTradeRingSize - 1] = b.trades[depthcharge::kTradeRingSize - 1];
    CHECK(torn_field(spliced) != nullptr);

    spliced = a;
    spliced.asks[depthcharge::kDisplayLevels - 1].qty = b.asks[depthcharge::kDisplayLevels - 1].qty;
    CHECK(torn_field(spliced) != nullptr);

    // The version itself is what the checker derives from, so a frame carrying
    // the *wrong* version is caught by everything else disagreeing with it.
    spliced = a;
    spliced.version = b.version;
    CHECK(torn_field(spliced) != nullptr);
}
