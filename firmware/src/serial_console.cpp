// firmware/src/serial_console.cpp — see serial_console.hpp.
#include "serial_console.hpp"

#include <cstdio>

#include <depthcharge/decimal.hpp>

#include "esp_log.h"
#include "esp_timer.h"

namespace depthcharge::fw {
namespace {

constexpr const char* kTag = "panel";

// Steady-state cadence. Anvil publishes ~13.6 events/s and every one of them is
// a published frame, so printing them all would be ~1 KB/s of log and unreadable
// on the bench. One line a second is enough to see the version advancing; every
// status transition still prints the instant it happens.
constexpr std::int64_t kLinePeriodUs = 1000000;
constexpr std::int64_t kStatsPeriodUs = 10000000;

// The heap baseline is taken a little after the first live frame, so that the
// TLS session, the first snapshot and any one-off buffers are all behind us and
// the window really is steady state (invariant #7's "after connect + first
// snapshot").
constexpr std::uint32_t kFramesBeforeHeapBaseline = 50;

const char* reason_name(GapReason r) noexcept {
    switch (r) {
        case GapReason::SeqGap:       return "seq-gap";
        case GapReason::ChecksumFail: return "checksum";
        case GapReason::Disconnect:   return "disconnect";
        case GapReason::Overflow:     return "overflow";
        case GapReason::Resync:       return "resync";
    }
    return "?";
}

// Integer ticks -> text, on the stack. See the header for why this is not
// String and not %f.
struct PriceText {
    char buf[kMaxFormattedChars + 1]{};

    PriceText(PriceTicks px, std::int32_t decimals) noexcept {
        const std::size_t n = format_scaled(px, static_cast<int>(decimals), buf, sizeof buf - 1);
        buf[n] = '\0';
        if (n == 0) { buf[0] = '?'; buf[1] = '\0'; }
    }
};

}  // namespace

bool SerialConsole::start(std::uint32_t stack_bytes, UBaseType_t priority) noexcept {
    const BaseType_t ok = xTaskCreatePinnedToCore(&SerialConsole::trampoline, "dc_panel",
                                                  stack_bytes, this, priority, nullptr, 1);
    return ok == pdPASS;
}

void SerialConsole::trampoline(void* self) noexcept {
    static_cast<SerialConsole*>(self)->run();
}

void SerialConsole::run() noexcept {
    ESP_LOGI(kTag, "console consumer up on core %d (stage D replaces this with HUB75)",
             xPortGetCoreID());

    for (;;) {
        // consume() reports nothing-new rather than blocking, which is what lets
        // this task idle instead of spin. Stage D's render task will do the same
        // and only touch the panel when a new version actually arrives.
        if (channel_.consume(received_)) {
            draw(received_);
        }

        const std::int64_t now = esp_timer_get_time();
        if (now - last_stats_us_ >= kStatsPeriodUs) {
            last_stats_us_ = now;
            print_stats();
        }

        // 20 ms: far finer than Anvil's ~80 ms cadence, so no frame waits long,
        // and coarse enough that the task is asleep almost all the time. Stage D
        // will drive this off the panel's frame period instead.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void SerialConsole::draw(const DisplaySnapshot& snap) noexcept {
    ++frames_drawn_;
    const std::int64_t now = esp_timer_get_time();

    // --- the transitions, which are the acceptance evidence -----------------
    const bool changed = !have_seen_frame_ || snap.status != last_status_;
    if (changed) {
        if (snap.status == FeedStatus::Stale) {
            stale_since_us_ = now;
            ESP_LOGW(kTag, "*** STALE (%s) at v%u — panel greys here ***",
                     reason_name(snap.stale_reason), static_cast<unsigned>(snap.version));
        } else {
            const std::int64_t grey_us = have_seen_frame_ ? (now - stale_since_us_) : 0;
            ESP_LOGI(kTag, "*** LIVE at v%u%s ***", static_cast<unsigned>(snap.version),
                     have_seen_frame_ ? "" : " (first frame)");
            if (have_seen_frame_ && grey_us > 0) {
                ESP_LOGI(kTag, "    grey for %d ms before resync",
                         static_cast<int>(grey_us / 1000));
            }
        }
        have_seen_frame_ = true;
        last_status_ = snap.status;
        last_line_us_ = 0;  // force a detail line immediately after a transition
    }

    if (now - last_line_us_ < kLinePeriodUs) { return; }
    last_line_us_ = now;

    // --- the steady-state line ----------------------------------------------
    if (snap.has_top()) {
        const PriceText bid(snap.best_bid(), snap.symbol.price_decimals);
        const PriceText ask(snap.best_ask(), snap.symbol.price_decimals);
        const PriceText last(snap.last_px, snap.symbol.price_decimals);
        ESP_LOGI(kTag, "v%-6u seq=%-6llu %-5s  bid %s x%lld | ask %s x%lld  spread=%lld  last=%s  tape=%u",
                 static_cast<unsigned>(snap.version),
                 static_cast<unsigned long long>(snap.seq),
                 snap.live() ? "LIVE" : "STALE",
                 bid.buf, static_cast<long long>(snap.bids[0].qty),
                 ask.buf, static_cast<long long>(snap.asks[0].qty),
                 static_cast<long long>(snap.spread_ticks()),
                 snap.has_last ? last.buf : "-",
                 static_cast<unsigned>(snap.trade_count));
    } else {
        ESP_LOGI(kTag, "v%-6u seq=%-6llu %-5s  (no book yet)",
                 static_cast<unsigned>(snap.version),
                 static_cast<unsigned long long>(snap.seq),
                 snap.live() ? "LIVE" : "STALE");
    }

    // Arm the heap window once the feed is genuinely in steady state.
    if (!heap_.armed() && snap.live() && frames_drawn_ >= kFramesBeforeHeapBaseline) {
        frames_at_baseline_ = frames_drawn_;
        heap_.mark_baseline();
    }
}

void SerialConsole::print_stats() noexcept {
    const auto& a = feed_.adapter_stats();
    const auto& b = feed_.book_stats();
    const auto& f = feed_.stats();
    const auto& p = pipe_.stats();

    ESP_LOGI(kTag, "-- adapter: in=%llu out=%llu snap=%llu book=%llu trade=%llu summary=%llu",
             static_cast<unsigned long long>(a.frames_in),
             static_cast<unsigned long long>(a.events_out),
             static_cast<unsigned long long>(a.snapshot_frames),
             static_cast<unsigned long long>(a.book_frames),
             static_cast<unsigned long long>(a.trade_frames),
             static_cast<unsigned long long>(a.summary_ignored));
    ESP_LOGI(kTag, "-- errors : parse=%llu price=%llu ticker=%llu unknown=%llu trunc=%llu backseq=%llu",
             static_cast<unsigned long long>(a.parse_errors),
             static_cast<unsigned long long>(a.price_errors),
             static_cast<unsigned long long>(a.other_ticker),
             static_cast<unsigned long long>(a.unknown_kind),
             static_cast<unsigned long long>(a.truncated_frames),
             static_cast<unsigned long long>(a.wire_seq_backward));
    ESP_LOGI(kTag, "-- book   : adopted=%llu trades=%llu gaps=%llu publishes=%llu",
             static_cast<unsigned long long>(b.snapshots_adopted),
             static_cast<unsigned long long>(b.trades_applied),
             static_cast<unsigned long long>(b.gaps),
             static_cast<unsigned long long>(b.publishes));
    ESP_LOGI(kTag, "-- feed   : frames=%u wd_gaps=%u sock_gaps=%u connects=%u worst_gap=%u ms worst_frame=%u us",
             static_cast<unsigned>(f.frames_in), static_cast<unsigned>(f.watchdog_gaps),
             static_cast<unsigned>(f.socket_gaps), static_cast<unsigned>(f.connects),
             static_cast<unsigned>(f.worst_gap_us / 1000),
             static_cast<unsigned>(f.worst_parse_us));
    ESP_LOGI(kTag, "-- pipe   : published=%u oversize=%u no_slot=%u qfull=%u abandoned=%u cont=%u ctrl=%u",
             static_cast<unsigned>(p.frames_published), static_cast<unsigned>(p.oversize),
             static_cast<unsigned>(p.no_slot), static_cast<unsigned>(p.queue_full),
             static_cast<unsigned>(p.abandoned), static_cast<unsigned>(p.continuation),
             static_cast<unsigned>(p.control));
    ESP_LOGI(kTag, "-- channel: published_v=%u consumed_v=%u drawn=%u",
             static_cast<unsigned>(channel_.published_version()),
             static_cast<unsigned>(channel_.consumed_version()),
             static_cast<unsigned>(frames_drawn_));

    heap_.report("steady", frames_drawn_ - frames_at_baseline_);
}

}  // namespace depthcharge::fw
