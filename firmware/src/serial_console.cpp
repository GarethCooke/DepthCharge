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

        // Before the timed block, because a hole's verdict is most useful while
        // the grey it caused is still on the screen in front of whoever is at
        // the bench.
        drain_holes(feed_.stats().stall);

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
    print_distributions(f, p);
    print_stall(f);
    ESP_LOGI(kTag, "-- pipe   : published=%u oversize=%u no_slot=%u qfull=%u abandoned=%u cont=%u ctrl=%u",
             static_cast<unsigned>(p.frames_published), static_cast<unsigned>(p.oversize),
             static_cast<unsigned>(p.no_slot), static_cast<unsigned>(p.queue_full),
             static_cast<unsigned>(p.abandoned), static_cast<unsigned>(p.continuation),
             static_cast<unsigned>(p.control));
    ESP_LOGI(kTag, "-- size   : msg min=%u max=%u B (cap %u), slots %u",
             static_cast<unsigned>(p.smallest_message),
             static_cast<unsigned>(p.largest_message),
             static_cast<unsigned>(kFrameCapacity),
             static_cast<unsigned>(kFrameSlots));
    ESP_LOGI(kTag, "-- channel: published_v=%u consumed_v=%u drawn=%u superseded=%u",
             static_cast<unsigned>(channel_.published_version()),
             static_cast<unsigned>(channel_.consumed_version()),
             static_cast<unsigned>(frames_drawn_),
             // The latest-value mailbox drops superseded frames silently by
             // design (ARCHITECTURE §9), so the count is not stored anywhere —
             // but it is recoverable, and it is the number people reach for when
             // they see `no_slot` and guess wrong. Naming it here stops that:
             // THIS is healthy consumer lag; `no_slot` above is inbound loss.
             static_cast<unsigned>(channel_.published_version() - frames_drawn_));

    print_rates(p, a.events_out);
    heap_.report("steady", frames_drawn_ - frames_at_baseline_);
}

void SerialConsole::print_distributions(const FeedTask::Stats& f,
                                        const FramePipeStats& p) noexcept {
    // THE THREE LINES THIS WHOLE SESSION EXISTS TO PRINT (strain 12).
    //
    // Read them together and in this order. `arrive` is the wire: whole
    // messages coming off the socket, counted where they land. `event` is the
    // ladder: the same silence the RX watchdog greys on. `a->e` is the bridge
    // between one message's arrival and the book having moved.
    //
    //   >1s filling on `event` with `arrive` clean       -> ours. The bytes came
    //       and the pipeline sat on them; then `a->e`, `qwait` and `backlog`
    //       say whether it was scheduling or work.
    //   `qwait` in seconds with `worst_frame` in millis  -> the feed task was
    //       not running: Core-0 starvation or a blocking call on this side.
    //   >1s filling on `arrive` and on `event` together  -> UNDECIDED, and the
    //       first draft of this comment said "transport", which is the reading
    //       the 2026-08-10 bench had to withdraw. `arrive` is stamped on the
    //       WebSocket client's task, downstream of the Wi-Fi driver, lwIP and
    //       the TLS decrypt, so it fills for a busy Core 0 too; and Anvil sheds
    //       to a slow consumer, so the server going quiet to us is a symptom of
    //       the same thing. Read the `cpu` and `holes` lines below for that
    //       fork; these three cannot settle it.
    //
    // Cumulative since boot, not per window — the question is "how often across
    // the whole run", and a distribution that resets every 10 s cannot answer it
    // for an event that happens twice a minute. The last block of a run is the
    // total.
    //
    print_gap_line("arrive", p.arrival_gaps);
    print_gap_line("event ", f.event_gaps);

    // 208 bytes: seven labelled counts, the longest of which is
    // "1.5-2.5k:4294967295". Truncation is defined and harmless, and this task
    // has 6 KiB of stack.
    char line[208];
    f.arrival_to_event.render(line, sizeof line);
    ESP_LOGI(kTag, "-- a->e   : %s | n=%u worst=%u ms | qwait=%u us behind=%u/%u msgs_in=%u",
             line, static_cast<unsigned>(f.arrival_to_event.total()),
             static_cast<unsigned>(f.arrival_to_event.worst_us() / 1000),
             static_cast<unsigned>(f.worst_queue_wait_us),
             static_cast<unsigned>(f.max_ready_backlog),
             static_cast<unsigned>(kReadyQueueDepth),
             static_cast<unsigned>(p.messages_arrived));
}

void SerialConsole::print_gap_line(const char* what, const GapHistogram& h) noexcept {
    // Both gap distributions print in exactly the same shape, deliberately:
    // reading them is a comparison, and a comparison between two lines with
    // different columns is a comparison someone gets wrong at 2 a.m.
    char line[208];
    h.render(line, sizeof line);
    ESP_LOGI(kTag, "-- %s : %s | n=%u worst=%u ms >1s=%u mode=%s",
             what, line, static_cast<unsigned>(h.total()),
             static_cast<unsigned>(h.worst_us() / 1000),
             static_cast<unsigned>(h.count_from(GapScale::kFirstLong)),
             GapHistogram::label(h.mode_from(GapScale::kFirstLong)));
}

void SerialConsole::print_stall(const FeedTask::Stats& f) noexcept {
    // THE THREE LINES THAT PICK THE NEXT BRIEF.
    //
    // Read `cpu` first and only then `holes`. The tally's board-bound /
    // link-bound split is derived from the per-hole idle against the healthy
    // baseline on the `cpu` line, so a baseline that looks wrong — Core 0 at 30%
    // idle in steady state, say — invalidates the tally above it rather than
    // being a separate observation.
    //
    // `window` is this task's own reading over the last statistics period, from
    // the same counters by a different task. It is not redundant: the per-hole
    // figures are a handful of one-second windows on Core 0 and this is ten
    // seconds of wall clock on Core 1, so the two disagreeing is the instrument
    // reporting a problem with itself.
    const std::int64_t now = esp_timer_get_time();
    const std::uint32_t idle0 = idle_.idle_us(0);
    const std::uint32_t idle1 = idle_.idle_us(1);
    if (block_started_us_ != 0 && now > block_started_us_) {
        const std::uint32_t window_us =
            clamp_us_to_u32(static_cast<std::uint64_t>(now - block_started_us_));
        const std::uint32_t d0 = wrapping_delta_u32(idle0, idle0_at_block_);
        const std::uint32_t d1 = wrapping_delta_u32(idle1, idle1_at_block_);
        if (idle_.valid()) {
            ESP_LOGI(kTag,
                     "-- cpu    : window c0=%u%% c1=%u%% over %u ms | healthy c0=%u%% c1=%u%% n=%u"
                     " | probe %u passes worst %u cyc of %u",
                     static_cast<unsigned>(idle_percent(d0, window_us)),
                     static_cast<unsigned>(idle_percent(d1, window_us)),
                     static_cast<unsigned>(window_us / 1000u),
                     static_cast<unsigned>(f.stall.baseline0_pct()),
                     static_cast<unsigned>(f.stall.baseline1_pct()),
                     static_cast<unsigned>(f.stall.baseline_windows()),
                     static_cast<unsigned>(idle_.accumulator(0).passes()),
                     static_cast<unsigned>(idle_.accumulator(0).worst_pass_cycles()),
                     static_cast<unsigned>(idle_.accumulator(0).continuity_cycles()));
        } else {
            ESP_LOGW(kTag, "-- cpu    : idle probe NOT RUNNING — every hole verdict is unknown");
        }
    }
    block_started_us_ = now;
    idle0_at_block_ = idle0;
    idle1_at_block_ = idle1;

    ESP_LOGI(kTag, "-- rssi   : now %d min %d max %d dBm n=%u",
             static_cast<int>(link_.last_dbm()), static_cast<int>(link_.min_dbm()),
             static_cast<int>(link_.max_dbm()), static_cast<unsigned>(link_.samples()));

    // 160 bytes: the tally is seven counts and a rate, the longest of which is
    // ten digits. Truncation is defined and harmless.
    char line[160];
    f.stall.render_tally(line, sizeof line);
    ESP_LOGI(kTag, "-- holes  : %s", line);
}

void SerialConsole::drain_holes(const StallProbe& stall) noexcept {
    // A record can be evicted from the ring before this task reaches it — it
    // cannot happen at two holes a minute against a 20 ms poll, but a silently
    // skipped verdict is exactly the sort of thing that gets noticed as "the
    // counts do not add up" three sessions later, so it is reported.
    if (holes_printed_ < stall.oldest_retained()) {
        ESP_LOGW(kTag, "-- hole   : %u verdicts evicted before printing",
                 static_cast<unsigned>(stall.oldest_retained() - holes_printed_));
        holes_printed_ = stall.oldest_retained();
    }
    while (holes_printed_ < stall.completed()) {
        const HoleRecord* r = stall.completed_at(holes_printed_);
        ++holes_printed_;
        if (r == nullptr) { continue; }
        char line[208];
        StallProbe::render_hole(*r, line, sizeof line);
        ESP_LOGW(kTag, "-- hole   : %s", line);
    }
}

void SerialConsole::print_rates(const FramePipeStats& p, std::uint64_t events_out) noexcept {
    const std::int64_t now = esp_timer_get_time();
    const std::uint32_t attempted = p.frames_published + p.no_slot + p.oversize;

    Window cur;
    cur.at_us = now;
    cur.published = p.frames_published;
    cur.attempted = attempted;
    cur.bytes = p.bytes_published;
    cur.chunks = p.chunks;
    cur.events = events_out;
    cur.drawn = frames_drawn_;

    if (have_prev_ && now > prev_.at_us) {
        // Integer arithmetic throughout — invariant #3's habit, and on this
        // target a float here would drag in soft-float for a log line. Rates are
        // printed in hundredths so "5.59 msg/s" stays readable without one.
        const std::uint64_t dt_ms = static_cast<std::uint64_t>((now - prev_.at_us) / 1000);
        if (dt_ms == 0) { prev_ = cur; return; }

        const auto per_s_x100 = [dt_ms](std::uint64_t delta) -> std::uint64_t {
            return (delta * 100000ull) / dt_ms;
        };
        const std::uint32_t d_pub = cur.published - prev_.published;
        const std::uint32_t d_att = cur.attempted - prev_.attempted;
        const std::uint64_t d_bytes = cur.bytes - prev_.bytes;
        const std::uint32_t d_chunks = cur.chunks - prev_.chunks;
        const std::uint64_t d_events = cur.events - prev_.events;

        const std::uint64_t pub_x100 = per_s_x100(d_pub);
        const std::uint64_t att_x100 = per_s_x100(d_att);
        const std::uint64_t ev_x100 = per_s_x100(d_events);
        // bytes/s, then KiB/s in hundredths. Two steps rather than one clever
        // expression because the first draft folded the decimal and binary
        // thousands together and printed a number that was wrong by 2.4%.
        const std::uint64_t bytes_per_s = (d_bytes * 1000ull) / dt_ms;
        const std::uint64_t kib_x100 = (bytes_per_s * 100ull) / 1024ull;
        // chunks per message, x100 — ~300 means the 4 KiB RX buffer is really
        // splitting ~8 KB frames three ways and reassembly is doing work.
        const std::uint32_t chunks_x100 =
            (d_pub != 0) ? static_cast<std::uint32_t>((d_chunks * 100ull) / d_pub) : 0;
        const std::uint32_t loss_pct =
            (d_att != 0) ? static_cast<std::uint32_t>(((d_att - d_pub) * 100ull) / d_att) : 0;
        const std::uint32_t mean_bytes =
            (d_pub != 0) ? static_cast<std::uint32_t>(d_bytes / d_pub) : 0;

        ESP_LOGI(kTag,
                 "-- rate   : in %u.%02u/s of %u.%02u/s attempted (%u%% lost) | events %u.%02u/s"
                 " | %u.%02u KB/s | mean %u B | %u.%02u chunks/msg | window %u ms",
                 static_cast<unsigned>(pub_x100 / 100), static_cast<unsigned>(pub_x100 % 100),
                 static_cast<unsigned>(att_x100 / 100), static_cast<unsigned>(att_x100 % 100),
                 static_cast<unsigned>(loss_pct),
                 static_cast<unsigned>(ev_x100 / 100), static_cast<unsigned>(ev_x100 % 100),
                 static_cast<unsigned>(kib_x100 / 100), static_cast<unsigned>(kib_x100 % 100),
                 static_cast<unsigned>(mean_bytes),
                 static_cast<unsigned>(chunks_x100 / 100),
                 static_cast<unsigned>(chunks_x100 % 100),
                 static_cast<unsigned>(dt_ms));
    }

    prev_ = cur;
    have_prev_ = true;
}

}  // namespace depthcharge::fw
