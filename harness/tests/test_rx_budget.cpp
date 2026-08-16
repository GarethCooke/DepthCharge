// harness/tests/test_rx_budget.cpp — the RX-loop stopwatch's arithmetic,
// proven on the desk before the bench reads a single line off it. The
// instrument exists to settle where the ~44 KiB/s inbound ceiling lives
// (ARCHITECTURE §9 2026-08-14); an instrument whose percentages were wrong by
// construction would send the next brief at the wrong stage, so the render
// math is pinned here, not eyeballed on a serial log.
#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "../../firmware/src/rx_budget.hpp"

using depthcharge::fw::RxBudget;
using depthcharge::fw::render_rx_budget;

TEST_CASE("accumulation: sums, counts, worsts") {
    RxBudget b;
    b.on_wait(100000);
    b.on_read(2000, 4096);
    b.on_read(6000, 1436);
    b.on_feed(3000);
    b.on_feed(1000);

    CHECK(b.wait_us == 100000);
    CHECK(b.read_us == 8000);
    CHECK(b.feed_us == 4000);
    CHECK(b.bytes == 5532);
    CHECK(b.reads == 2);
    CHECK(b.waits == 1);
    CHECK(b.worst_read_us == 6000);
    CHECK(b.worst_feed_us == 3000);
}

TEST_CASE("a negative or absurd stopwatch reading clamps instead of lying") {
    RxBudget b;
    b.on_read(-5, 100);          // clock misread: contributes zero time, real bytes
    b.on_feed(-1);
    b.on_wait(0x7FFFFFFFLL + 50);  // clamped to the int32 ceiling, not wrapped
    CHECK(b.read_us == 0);
    CHECK(b.feed_us == 0);
    CHECK(b.bytes == 100);
    CHECK(b.wait_us == 0x7FFFFFFFu);
}

TEST_CASE("render: percentages are of the window, and the shortfall is `other`") {
    RxBudget prev;
    RxBudget now;
    // A 10-second window: 6 s waiting, 1 s reading, 0.5 s feeding — 2.5 s
    // unaccounted, which must surface as other=25, not vanish.
    now.wait_us = 6000000;
    now.read_us = 1000000;
    now.feed_us = 500000;
    now.bytes = 440 * 1024;
    now.reads = 110;
    now.waits = 40;
    now.worst_read_us = 20000;
    now.worst_feed_us = 12000;

    char line[160];
    const int n = render_rx_budget(now, prev, 10000000ull, line, sizeof line);
    REQUIRE(n > 0);
    const std::string s(line);
    CHECK(s.find("wait 60%") != std::string::npos);
    CHECK(s.find("read 10%") != std::string::npos);
    CHECK(s.find("feed 5%") != std::string::npos);
    CHECK(s.find("other 25%") != std::string::npos);
    CHECK(s.find("110 reads 40 waits") != std::string::npos);
    CHECK(s.find("mean 4096 B") != std::string::npos);
    CHECK(s.find("44.0 KiB/s") != std::string::npos);
}

TEST_CASE("render diffs cumulative counters, so the second window stands alone") {
    RxBudget prev;
    prev.wait_us = 1000000;
    prev.read_us = 1000000;
    prev.feed_us = 1000000;
    prev.bytes = 1024;
    prev.reads = 1;
    prev.waits = 1;

    RxBudget now = prev;
    now.read_us += 5000000;   // this window: all read, nothing else
    now.bytes += 10240;
    now.reads += 10;

    char line[160];
    REQUIRE(render_rx_budget(now, prev, 5000000ull, line, sizeof line) > 0);
    const std::string s(line);
    CHECK(s.find("wait 0%") != std::string::npos);
    CHECK(s.find("read 100%") != std::string::npos);
    CHECK(s.find("other 0%") != std::string::npos);
    CHECK(s.find("10 reads 0 waits") != std::string::npos);
    CHECK(s.find("mean 1024 B") != std::string::npos);
}

TEST_CASE("a zero window and a zero cap are defined, not divisions") {
    RxBudget b;
    char line[160];
    CHECK(render_rx_budget(b, b, 0, line, sizeof line) > 0);
    CHECK(render_rx_budget(b, b, 1000, nullptr, 0) == 0);
}
