// firmware/src/render_task.cpp — see render_task.hpp.
#include "render_task.hpp"

#include <cstdio>

#include "esp_log.h"
#include "esp_timer.h"

namespace depthcharge::fw {
namespace {

constexpr const char* kTag = "panel";

// The two engine clocks reach this file only to be PRINTED — `feed_.liveness()`
// hands back a const reference and there is nothing on it this task could steer.
using depthcharge::AgeText;

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

// Integer ticks -> text on the stack is ladder_render.hpp's TextField, and this
// file no longer carries its own copy of it. The two were the same eight lines —
// same buffer sized from kMaxFormattedChars, same format_scaled call, same '?'
// fallback — which is two implementations of the display edge's one rule
// (invariants #3 and #7: no String, no %f, no heap) inside a firmware whose
// panel and whose log have to agree about what a price is.

}  // namespace

bool RenderTask::start(std::uint32_t stack_bytes, UBaseType_t priority) noexcept {
    const BaseType_t ok = xTaskCreatePinnedToCore(&RenderTask::trampoline, "dc_panel",
                                                  stack_bytes, this, priority, nullptr, 1);
    return ok == pdPASS;
}

void RenderTask::trampoline(void* self) noexcept {
    static_cast<RenderTask*>(self)->run();
}

void RenderTask::run() noexcept {
    ESP_LOGI(kTag, "render task up on core %d, %u ms frame period, panel %s",
             xPortGetCoreID(), static_cast<unsigned>(kFramePeriodMs),
             panel_.up() ? "UP" : "ABSENT (serial evidence only)");

    // THE BOOT FRAME, BEFORE ANY DATA. `received_` is a value-initialised
    // DisplaySnapshot, which is Stale{Resync} with no levels by construction —
    // exactly what the feed task publishes as v1, and exactly what stage D
    // requires on screen while the socket is coming up: an honest grey empty
    // panel, not a black one. Painted here rather than waiting for the first
    // consume() so the object is never dark while it is working.
    paint(received_);

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        // consume() reports nothing-new rather than blocking, which is what lets
        // this task idle instead of spin, and it is what makes the redraw
        // version-gated: the panel is only ever touched when the book moved.
        if (channel_.consume(received_)) {
            draw(received_);
        }

        // Before the timed block, because a hole's verdict is most useful while
        // the grey it caused is still on the screen in front of whoever is at
        // the bench.
        drain_holes(feed_.stats().stall);
        drain_rejects(feed_.stats().rejects);

        const std::int64_t now = esp_timer_get_time();
        if (now - last_stats_us_ >= kStatsPeriodUs) {
            last_stats_us_ = now;
            print_stats();
        }

        // The panel's period, not a flat delay: an absolute deadline keeps the
        // cadence honest when a statistics block has just cost this task a few
        // milliseconds of UART. If it ever falls a whole period behind — which a
        // long blocking call on this core would do — the deadline is re-based
        // rather than chased, because catching up would mean several redraws
        // back to back and the panel gains nothing from any but the last.
        const TickType_t period = pdMS_TO_TICKS(kFramePeriodMs);
        const TickType_t nowtick = xTaskGetTickCount();
        if (static_cast<TickType_t>(nowtick - wake) > period) { wake = nowtick; }
        vTaskDelayUntil(&wake, period);
    }
}

// THE ONE PALETTE SELECTION IN THE FIRMWARE (invariant #5).
//
// snap.status picks a Palette, the Palette goes into the canvas, and the
// renderer downstream of it can only name an Ink. kStalePalette is proven
// entirely grey at compile time (ladder_render.hpp), so there is no path from a
// stale snapshot to a coloured pixel that this build would accept. Nothing else
// in firmware/ reads status to choose a colour, and nothing needs to.
void RenderTask::paint(const DisplaySnapshot& snap) noexcept {
    if (!panel_.up()) { return; }

    const std::int64_t started = esp_timer_get_time();

    const Palette palette = palette_for(snap.status);
    PanelCanvas canvas(panel_.driver(), palette);
    view_.observe(snap);
    view_.draw(snap, canvas);
    panel_.present();

    const std::uint32_t took =
        clamp_us_to_u32(static_cast<std::uint64_t>(esp_timer_get_time() - started));
    if (took > worst_paint_us_) { worst_paint_us_ = took; }
}

void RenderTask::draw(const DisplaySnapshot& snap) noexcept {
    ++frames_drawn_;

    // THE PANEL FIRST. Everything below this line is a bench instrument that
    // takes the UART mutex and blocks for milliseconds; the pixels are the
    // deliverable and must not queue behind them. It also means the grey lands
    // on the panel before the log line that explains it, which is the right way
    // round for someone watching both.
    paint(snap);

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
        const int dp = static_cast<int>(snap.symbol.price_decimals);
        const TextField bid = TextField::scaled(snap.best_bid(), dp);
        const TextField ask = TextField::scaled(snap.best_ask(), dp);
        const TextField last = TextField::scaled(snap.last_px, dp);
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

void RenderTask::print_stats() noexcept {
    const auto& a = feed_.adapter_stats();
    const auto& b = feed_.book_stats();
    const auto& f = feed_.stats();
    const auto& p = pipe_.stats();

    // THE TWO VENUE-SHAPED LINES, AND THE ONLY `#if` IN THIS FILE. Everything
    // else the console prints is venue-agnostic by construction; adapter counters
    // are not, because the counters themselves differ — Anvil has `summary` and
    // `other_ticker`, Kraken has `heartbeats`, four checksum columns and an
    // eviction count. Printing a common subset would throw away exactly the
    // numbers a bench evening is there to read, and inventing a shared shape
    // would put venue vocabulary somewhere `engine/` could not check it.
#if DC_VENUE == DC_VENUE_KRAKEN
    ESP_LOGI(kTag, "-- adapter: in=%llu out=%llu snap=%llu upd=%llu beat=%llu ack=%llu/%llu",
             static_cast<unsigned long long>(a.frames_in),
             static_cast<unsigned long long>(a.events_out),
             static_cast<unsigned long long>(a.snapshot_frames),
             static_cast<unsigned long long>(a.update_frames),
             static_cast<unsigned long long>(a.heartbeats),
             static_cast<unsigned long long>(a.acks),
             static_cast<unsigned long long>(a.unsubscribe_acks));
    // THE CHECKSUM LEDGER, AND ITS IDENTITY IS THE POINT: seen == matched +
    // failed + unverifiable. `unverifiable` is not `failed` — it is a book
    // message arriving before this client had a baseline to check it against —
    // and collapsing the two is what would make a mid-stream start look like
    // corruption. `unchecksummed` should be 0 on this wire; anything else means
    // a book message arrived without the field the healing path depends on.
    ESP_LOGI(kTag, "-- crc    : seen=%llu ok=%llu FAIL=%llu unverifiable=%llu unchecksummed=%llu"
                   " | resyncs=%llu",
             static_cast<unsigned long long>(a.checksums_seen),
             static_cast<unsigned long long>(a.checksums_matched),
             static_cast<unsigned long long>(a.checksums_failed),
             static_cast<unsigned long long>(a.checksums_unverifiable),
             static_cast<unsigned long long>(a.book_msgs_unchecksummed),
             static_cast<unsigned long long>(a.resyncs_requested));
    ESP_LOGI(kTag, "-- errors : parse=%llu price=%llu qty=%llu symbol=%llu unknown=%llu"
                   " | levels applied=%llu removed=%llu evicted=%llu deeper=%llu",
             static_cast<unsigned long long>(a.parse_errors),
             static_cast<unsigned long long>(a.price_errors),
             static_cast<unsigned long long>(a.qty_errors),
             static_cast<unsigned long long>(a.other_symbol),
             static_cast<unsigned long long>(a.unknown_kind),
             static_cast<unsigned long long>(a.levels_applied),
             static_cast<unsigned long long>(a.levels_removed),
             static_cast<unsigned long long>(a.levels_evicted),
             static_cast<unsigned long long>(a.levels_deeper_than_subscribed));
#elif DC_VENUE == DC_VENUE_BINANCE
    ESP_LOGI(kTag, "-- adapter: in=%llu out=%llu diff=%llu partial=%llu rest=%llu/%llu",
             static_cast<unsigned long long>(a.frames_in),
             static_cast<unsigned long long>(a.events_out),
             static_cast<unsigned long long>(a.diff_frames),
             static_cast<unsigned long long>(a.partial_frames),
             static_cast<unsigned long long>(a.rest_snapshots),
             static_cast<unsigned long long>(a.rest_no_body));
    // THE SEED LEDGER, AND IT IS THIS VENUE'S ANSWER TO KRAKEN'S CRC LINE.
    // Binance publishes no checksum, so there is no ledger that says "is my
    // book the venue's book?" — what there is instead is the bracket, and
    // `seeds_unconfirmed` is the number M5 stage D-A1's acceptance is read off:
    // a socket that opens, answers pings and never delivers a bracketing diff
    // leaves it climbing while the ladder stays honestly grey. Until D-A2
    // builds the REST client there is no seed to confirm, so `bracket ok/FAIL`
    // reads 0/0 and `unconfirmed` counts — that is the expected shape of this
    // line on a D-A1 board, not a fault.
    ESP_LOGI(kTag, "-- seed   : buffered=%llu dropped=%llu overflow=%llu"
                   " | bracket ok=%llu FAIL=%llu unconfirmed=%llu reseeds=%llu",
             static_cast<unsigned long long>(a.buffered_events),
             static_cast<unsigned long long>(a.buffered_dropped_by_seed),
             static_cast<unsigned long long>(a.buffer_overflows),
             static_cast<unsigned long long>(a.seed_bracket_ok),
             static_cast<unsigned long long>(a.seed_bracket_failed),
             static_cast<unsigned long long>(a.seeds_unconfirmed),
             static_cast<unsigned long long>(a.reseeds_requested));
    // THE RE-SEED LEDGER (M5 stage D-A4), AND IT IS THE ONLY PLACE THE
    // MECHANISM IS VISIBLE — which is a finding rather than a preference.
    //
    // D-B decision 2 put a marker in the header for a re-seed in flight, and
    // D-A4 measured that the marker cannot be drawn on this build: Binance's
    // `price_decimals` is 8, so a live BTCUSDT last price is fifteen characters
    // and 74 px on a 64 px panel, and the value slot takes the whole header.
    // The panel therefore says nothing about a re-seed, and this line is what a
    // bench has instead. It is on its own line rather than appended to the seed
    // ledger above because the two answer different questions — that one is
    // "did the seed arrive and did the feed corroborate it", this one is "did a
    // re-seed land on a live book without dropping it".
    //
    // `adopted` climbing with `greys` flat is the whole claim of the stage.
    ESP_LOGI(kTag, "-- reseed : adopted=%llu unbracketed=%llu hold-overflow=%llu"
                   " | declined(no-hold)=%llu adoptable=%llu | triggers=%llu",
             static_cast<unsigned long long>(a.reseeds_adopted),
             static_cast<unsigned long long>(a.reseeds_unbracketed),
             static_cast<unsigned long long>(a.reseed_holds_overflowed),
             static_cast<unsigned long long>(a.resnapshots_declined),
             static_cast<unsigned long long>(a.resnapshots_adoptable),
             static_cast<unsigned long long>(a.cover_triggers));
    ESP_LOGI(kTag, "-- errors : parse=%llu price=%llu qty=%llu symbol=%llu unknown=%llu"
                   " | seqbreak=%llu overflow=%llu | levels applied=%llu removed=%llu evicted=%llu",
             static_cast<unsigned long long>(a.parse_errors),
             static_cast<unsigned long long>(a.price_errors),
             static_cast<unsigned long long>(a.qty_errors),
             static_cast<unsigned long long>(a.other_symbol),
             static_cast<unsigned long long>(a.unknown_kind),
             static_cast<unsigned long long>(a.seq_breaks),
             static_cast<unsigned long long>(a.overflow_frames),
             static_cast<unsigned long long>(a.levels_applied),
             static_cast<unsigned long long>(a.levels_removed),
             static_cast<unsigned long long>(a.levels_evicted));
#else
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
#endif
    print_rejects(f.rejects);
    ESP_LOGI(kTag, "-- book   : adopted=%llu trades=%llu gaps=%llu publishes=%llu",
             static_cast<unsigned long long>(b.snapshots_adopted),
             static_cast<unsigned long long>(b.trades_applied),
             static_cast<unsigned long long>(b.gaps),
             static_cast<unsigned long long>(b.publishes));
    ESP_LOGI(kTag, "-- feed   : frames=%u wd_gaps=%u sock_gaps=%u connects=%u worst_gap=%u ms",
             static_cast<unsigned>(f.frames_in), static_cast<unsigned>(f.watchdog_gaps),
             static_cast<unsigned>(f.socket_gaps), static_cast<unsigned>(f.connects),
             static_cast<unsigned>(f.worst_gap_us / 1000));

    // FRAME TIME AS A DISTRIBUTION, because the maximum alone could not
    // distinguish "23x slower" from "unchanged with a rare outlier" and cost two
    // evenings proving it was the second.
    //
    // p99 is reported as the BUCKET it falls in, never interpolated inside one:
    // these are fixed-bucket counts and a two-decimal p99 would carry more
    // digits than information. `slow` counts frames at or past 25 ms, the point
    // at which four consecutive ones consume a whole arrival interval and the
    // pipe stops gaining ground — see FrameScale.
    //
    // `worst` is WALL-CLOCK across `on_frame`, so it absorbs any preemption of
    // the feed task, and Wi-Fi and lwIP run far above its priority 5.
    // `quiet`/`fetch` split it by whether a seed fetch was in flight, which is
    // what told "the feed path got slower" apart from "something else was
    // hammering the network stack" — measured: the peaks are quiet.
    {
        const std::size_t p99 = f.frame_times.percentile_bucket(99);
        ESP_LOGI(kTag, "-- frame  : p99=%s worst=%u us slow(>25ms)=%u of %u max_run=%u of %u slots"
                       " | quiet=%u fetch=%u over %u frames",
                 f.frame_times.label(p99),
                 static_cast<unsigned>(f.worst_parse_us),
                 static_cast<unsigned>(f.frame_times.count_from(FrameScale::kFirstLong)),
                 static_cast<unsigned>(f.frame_times.total()),
                 // The run length is what turns "slow" into "dropped": the pipe
                 // absorbs isolated slow frames and stops gaining ground once a
                 // run reaches kFrameSlots. Printed against that denominator so
                 // the reading needs no arithmetic at the bench.
                 static_cast<unsigned>(f.slow_run.worst()),
                 static_cast<unsigned>(kFrameSlots),
                 static_cast<unsigned>(f.worst_parse_quiet_us),
                 static_cast<unsigned>(f.worst_parse_fetch_us),
                 static_cast<unsigned>(f.frames_during_fetch));
        char buf[160];
        f.frame_times.render(buf, sizeof(buf));
        ESP_LOGI(kTag, "-- frames : %s", buf);
    }

    // SLOT RESIDENCY — acquire to release/recycle, which is the quantity the
    // two above only approximate. Frame time is the LAST term of a slot's
    // occupancy; a slot is held through reassembly and the ready queue before
    // the parse begins, which is why the board could drop (`no_slot`) with a
    // worst frame-run of 3 against 4 slots. `held(>100ms)` is one arrival
    // interval: a slot held longer than the gap between messages was not free
    // when the next one needed it.
    {
        const std::size_t rp99 = p.residency.percentile_bucket(99);
        ESP_LOGI(kTag, "-- slot   : p99=%s worst=%u us held(>100ms)=%u of %u",
                 p.residency.label(rp99),
                 static_cast<unsigned>(p.residency.worst_us()),
                 static_cast<unsigned>(p.residency.count_from(ResidencyScale::kFirstLong)),
                 static_cast<unsigned>(p.residency.total()));
        char buf[160];
        p.residency.render(buf, sizeof(buf));
        ESP_LOGI(kTag, "-- slots  : %s", buf);
    }
    print_soak(f, a);
    print_distributions(f, p);
    print_stall(f);
    // `max_held` leads `no_slot`: the pipe reaching full happens first and
    // happens whether or not anything was dropped, so "4 of 4 with no_slot=0"
    // is a run that came within one message of dropping and nothing else on
    // this line would say so.
    ESP_LOGI(kTag, "-- pipe   : published=%u oversize=%u no_slot=%u max_held=%u of %u"
                   " qfull=%u abandoned=%u cont=%u ctrl=%u",
             static_cast<unsigned>(p.frames_published), static_cast<unsigned>(p.oversize),
             static_cast<unsigned>(p.no_slot),
             static_cast<unsigned>(p.max_held), static_cast<unsigned>(kFrameSlots),
             static_cast<unsigned>(p.queue_full),
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

    print_panel();
    print_rates(p, f, a.events_out);
    heap_.report("steady", frames_drawn_ - frames_at_baseline_);
}

void RenderTask::print_soak(const FeedTask::Stats& f,
                            const venue::Adapter::Stats& a) noexcept {
    const std::int64_t now = esp_timer_get_time();
    const LivenessWatchdog& lw = feed_.liveness();

    // THE AGE COMES FROM THE SNAPSHOT, NOT FROM THE ESTIMATOR, and review is
    // why. `AgeEstimator::read` walks 256 `int64_t` arrivals and reads a
    // `double` baseline; calling it from here means doing that while the feed
    // task pushes into the same ring on the other core, and a torn `double` is
    // a nonsense reference cadence and therefore a nonsense age. The value this
    // task is entitled to already crossed the boundary correctly — the feed side
    // stamped it into the `DisplaySnapshot` this task consumed, through the
    // wait-free mailbox that exists for exactly that (invariants #4 and #8).
    //
    // Everything else read off `lw` below is a 32-bit millisecond mirror the
    // feed task refreshes on each liveness arrival, which this core loads
    // atomically — see liveness_watchdog.hpp.
    const AgeText age_txt = AgeText::from(received_.has_age, received_.age_ms);
    const AgeText worst_txt = AgeText(lw.worst_age_ms());
    const std::uint64_t grey_ms = f.grey.total_ms(now);
    const HeapSample heap = sample_heap();

    // ROWS FILLED, AND ROWS WITHIN THE CHECKSUM'S REACH — summed over both sides
    // of the LAST publish. Stage C measured this over whole runs (37.0% for
    // `top` and `thinned` against 11.9% for `largest`, at depth 100); A4 asks the
    // board for its own, and at the shipped depth its own is a CONSTANT: 25
    // levels a side into 27 rows drops nothing, so all three policies coincide
    // and the fraction is simply what the venue validates over what it serves.
    // That constancy is the finding rather than a disappointment — the depth
    // question only exists above the panel's height, which is what stage C
    // handed to Part B to decide.
    const window::WindowStats& bids = feed_.bid_window();
    const window::WindowStats& asks = feed_.ask_window();
    const std::uint32_t filled = bids.rows_filled + asks.rows_filled;
    const std::uint32_t unknown = bids.rows_unknown + asks.rows_unknown;
    const std::uint32_t validated = bids.rows_validated + asks.rows_validated;
    const std::uint32_t pct_x10 = (filled != 0) ? (validated * 1000u) / filled : 0u;

    ESP_LOGI(kTag,
             "SOAK venue=%.*s up=%llus live=%d age=%s worst_age=%s baseline=%ums"
             " grey_n=%u grey_ms=%llu wd=%u sock=%u connects=%u"
             " rows=%u/%u unknown=%u crc_rows=%u (%u.%u%%)"
             " resync_req=%u heals=%u owed=%d refused=%u crc_fail=%llu"
             " heap=%u largest=%u frames=%u drawn=%u" DC_SOAK_TEST_TAG DC_SOAK_SILENT_TAG,
             static_cast<int>(venue::kName.size()), venue::kName.data(),
             static_cast<unsigned long long>(now / 1000000),
             (have_seen_frame_ && last_status_ == FeedStatus::Live) ? 1 : 0,
             age_txt.buf, worst_txt.buf,
             static_cast<unsigned>(lw.baseline_ms()),
             static_cast<unsigned>(f.grey.episodes()),
             static_cast<unsigned long long>(grey_ms),
             static_cast<unsigned>(f.watchdog_gaps),
             static_cast<unsigned>(f.socket_gaps),
             static_cast<unsigned>(f.connects),
             static_cast<unsigned>(filled),
             static_cast<unsigned>(filled + unknown),
             static_cast<unsigned>(unknown),
             static_cast<unsigned>(validated),
             static_cast<unsigned>(pct_x10 / 10), static_cast<unsigned>(pct_x10 % 10),
             static_cast<unsigned>(venue::resyncs_requested(a)),
             static_cast<unsigned>(subscription_.heals()),
             subscription_.owed() ? 1 : 0,
             static_cast<unsigned>(signal_.refusals()),
             static_cast<unsigned long long>(venue::checksum_failures(a)),
             static_cast<unsigned>(heap.free_internal),
             static_cast<unsigned>(heap.largest_block_internal),
             static_cast<unsigned>(f.frames_in),
             static_cast<unsigned>(frames_drawn_));

    // WHAT THE CHECKSUM CANNOT SEE, said in words on the build where the answer
    // is zero. `crc_rows=0 (0.0%)` reads as a failure; "this venue publishes no
    // checksum" reads as a property of the venue, which is what it is.
    if constexpr (venue::kValidatedDepth == 0) {
        ESP_LOGI(kTag,
                 "SOAK note: %.*s publishes no checksum, so NO rendered row on this"
                 " build was ever externally confirmed",
                 static_cast<int>(venue::kName.size()), venue::kName.data());
    }
}

void RenderTask::print_panel() noexcept {
    const PanelReport& r = panel_.report();
    if (!r.up) {
        ESP_LOGW(kTag, "-- panel  : NOT RUNNING — the ladder is serial-only this run");
        return;
    }

    // The draw rate is the number the feed-side regression check pairs with.
    // Anvil publishes ~13 frames/s and this task redraws once per published
    // version, so `drawn` per window should track `-- rate`'s events/s. Well
    // below it means the render task is not keeping up — a priority or placement
    // question, not an engine one. `worst` against the 33,000 us frame period is
    // the headroom: the paint is a fixed 64 hlines plus a header, so it should
    // not move with book depth, and it moving is itself the finding.
    const std::int64_t now = esp_timer_get_time();
    const std::uint32_t drawn = frames_drawn_ - drawn_at_block_;
    drawn_at_block_ = frames_drawn_;
    const std::uint64_t window_ms = (panel_block_us_ != 0 && now > panel_block_us_)
                                        ? static_cast<std::uint64_t>((now - panel_block_us_) / 1000)
                                        : 0;
    panel_block_us_ = now;
    const std::uint64_t fps_x100 = (window_ms > 0) ? (drawn * 100000ull) / window_ms : 0;

    ESP_LOGI(kTag,
             "-- panel  : depth=%u %s bright=%u refresh=%d Hz fb=%u B (free %u -> %u)"
             " | drew %u at %u.%02u/s worst paint %u us of %u us period",
             static_cast<unsigned>(r.colour_depth),
             r.double_buffered ? "double" : "SINGLE",
             static_cast<unsigned>(r.brightness), r.refresh_hz,
             static_cast<unsigned>(r.predicted_bytes),
             static_cast<unsigned>(r.free_before), static_cast<unsigned>(r.free_after),
             static_cast<unsigned>(drawn),
             static_cast<unsigned>(fps_x100 / 100), static_cast<unsigned>(fps_x100 % 100),
             static_cast<unsigned>(worst_paint_us_),
             static_cast<unsigned>(kFramePeriodMs * 1000u));
}

void RenderTask::print_distributions(const FeedTask::Stats& f,
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
    print_rx();

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

void RenderTask::print_rx() noexcept {
    // THE LINE THE 2026-08-14 CEILING VERDICT ASKED FOR (rx_budget.hpp): how
    // the RX task's window divided between waiting for bytes, decrypting them
    // and feeding them onward. Printed beside `arrive`/`event` because the
    // three are one reading — those two say the silence pattern, this says
    // what the loop was doing through it. `no reads` is stated rather than a
    // row of zeros, which would read as a broken instrument: it is the espws
    // arm (whose budget nothing updates) or a socket that was down all window.
    const std::int64_t now = esp_timer_get_time();
    const RxBudget snap = rx_;   // one copy; the RX task keeps writing
    if (rx_block_us_ != 0 && now > rx_block_us_) {
        if (snap.reads == rx_prev_.reads && snap.waits == rx_prev_.waits) {
            ESP_LOGI(kTag, "-- rx     : no reads this window (socket down, or the espws arm)");
        } else {
            // 160 bytes: four percentages, five counts, none past ten digits.
            char line[160];
            (void)render_rx_budget(snap, rx_prev_,
                                   static_cast<std::uint64_t>(now - rx_block_us_), line,
                                   sizeof line);
            ESP_LOGI(kTag, "-- rx     : %s", line);
        }
    }
    rx_prev_ = snap;
    rx_block_us_ = now;
}

void RenderTask::print_rejects(const RejectLog& rejects) noexcept {
    // SILENT ON A HEALTHY RUN, AND THAT IS NOT THE USUAL LAZINESS ABOUT ZEROES.
    // The `-- errors` line above already states "no frame was rejected", in the
    // `parse=0 price=0 ticker=0` it always prints; this line is the breakdown OF
    // a non-zero, so printing it empty every ten seconds would say the same
    // thing twice and bury the line that matters when it finally appears.
    //
    // The two lines are checkable against each other, which is the point of
    // having both: `n=` here must equal `parse + price + ticker` there. A
    // disagreement is a bug in one of these instruments, not something a reader
    // at the bench has to reconcile.
    if (rejects.total() == 0) { return; }
    // 208 bytes: six labelled counts with the longest label at twelve characters
    // and the longest count at ten digits, plus the trailer.
    char line[208];
    rejects.render_tally(line, sizeof line);
    ESP_LOGW(kTag, "-- reject : %s", line);
}

void RenderTask::print_gap_line(const char* what, const GapHistogram& h) noexcept {
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

void RenderTask::print_stall(const FeedTask::Stats& f) noexcept {
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
                     static_cast<unsigned>(window_us / kUsPerMs),
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

void RenderTask::drain_holes(const StallProbe& stall) noexcept {
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

void RenderTask::drain_rejects(const RejectLog& rejects) noexcept {
    // Eviction is reported rather than skipped, for the same reason the hole ring
    // reports it: a payload that was captured and then quietly lost reads at the
    // bench as "the log only shows nine of them", which is a question about the
    // instrument in the middle of reading it for an answer about the wire.
    if (rejects_printed_ < rejects.oldest_retained()) {
        ESP_LOGW(kTag, "-- reject : %u payloads evicted before printing",
                 static_cast<unsigned>(rejects.oldest_retained() - rejects_printed_));
        rejects_printed_ = rejects.oldest_retained();
    }
    while (rejects_printed_ < rejects.captured()) {
        const RejectRecord* r = rejects.at(rejects_printed_);
        ++rejects_printed_;
        if (r == nullptr) { continue; }
        // Sized by the renderer rather than by this call site, so widening the
        // captured head or tail cannot silently start truncating the line. This
        // task has 6 KiB of stack against its ~240 bytes.
        char line[kRejectLineChars];
        RejectLog::render(*r, line, sizeof line);
        ESP_LOGW(kTag, "-- reject : %s", line);
    }
}

void RenderTask::print_rates(const FramePipeStats& p, const FeedTask::Stats& f,
                             std::uint64_t events_out) noexcept {
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

        // HOW OLD THE BOOK IS, AND THE CLOCK THE ANSWER CAME FROM.
        //
        // Printed directly under `-- rate` because the two are one reading:
        // `rate` says how much of the stream is arriving and this says what that
        // costs in seconds. Every earlier instrument in this firmware measures
        // whether the feed is STOPPED; a feed at 41% of the broadcast is not
        // stopped and every counter above reads healthy while the panel shows a
        // book a hundred seconds old.
        //
        // REWRITTEN AT M4 STAGE D, AND THE FIELDS CHANGED WITH THE ARITHMETIC.
        // `drain %` and `summary N of M` are gone with the cumulative estimator
        // that produced them: they divided by a hardcoded 500 ms broadcast
        // period, which is Anvil's number and is wrong by 2x at Kraken. What
        // replaces them is the windowed estimator's own working:
        //
        //   `age`       the windowed deficit — the sup over every suffix of the
        //               last 256 liveness arrivals of (elapsed - n x baseline).
        //   `worst`     the largest ever seen, banked across reconnects, because
        //               the per-connection figure is destroyed every time the
        //               socket blinks and that erasure is how the 86-minute run
        //               of 2026-08-09 looked healthy.
        //   `baseline`  THIS connection's reference cadence, latched once from
        //               its first 32 intervals. Every age is `elapsed - n x
        //               baseline`, so a reader who cannot see the baseline cannot
        //               check the arithmetic — and a baseline that is not the
        //               venue's true interval is the one way this instrument
        //               lies (the socket-behind-from-birth blind spot, M6).
        //   `median`    the OTHER statistic taken from the same signal, and the
        //   `grey at`   threshold derived from it. It is a rolling median that
        //               survives a reconnect, where the baseline does not; the
        //               two must be visibly different numbers or nobody will
        //               believe they are measuring different things.
        //
        // `-` for the age means NO READING, not zero: before the baseline latches
        // the estimator has nothing to measure against, and printing 0.0s there
        // would be the one reassuring answer it is not entitled to give.
        //
        // `seq` is the JOIN KEY, not a diagnostic — see FeedTask::last_wire_seq.
        // With it on this line, the serial log and a simultaneous desk capture can
        // be joined offline into a continuous lag curve at the publish rate. It
        // reads -1 at a venue with no wire seq to join on.
        // Same rule as the SOAK line: the age is the snapshot's, everything
        // else is the 32-bit mirror. See print_soak.
        const LivenessWatchdog& lw = feed_.liveness();
        const AgeText age_txt = AgeText::from(received_.has_age, received_.age_ms);
        const AgeText worst_txt = AgeText(lw.worst_age_ms());
        ESP_LOGI(kTag,
                 "-- age    : %s (worst %s) | baseline %u ms | %.*s median %u ms,"
                 " grey at %u ms after %u sample(s)%s | back-stamps %u | seq %lld",
                 age_txt.buf, worst_txt.buf,
                 static_cast<unsigned>(lw.baseline_ms()),
                 static_cast<int>(venue::kLivenessSignal.size()), venue::kLivenessSignal.data(),
                 static_cast<unsigned>(lw.median_ms()),
                 static_cast<unsigned>(lw.threshold_ms()),
                 static_cast<unsigned>(lw.samples()),
                 lw.calibrated() ? "" : " UNCALIBRATED",
                 static_cast<unsigned>(lw.non_monotone()),
                 static_cast<long long>(feed_.last_wire_seq()));

        // DIRECTLY UNDER `-- age`, BECAUSE THE TWO ARE ONE READING. The age says
        // how old the book is; this says whether the age is sitting in Anvil's
        // send queue for THIS socket (rtt high — ROADMAP A7 is the fix) or
        // upstream of it in the broadcaster (rtt low — A7 would not help).
        // Neither number alone supports either conclusion, which is why they are
        // printed adjacent and why ws_ping.hpp's header carries the 2x2.
        //
        // 128 bytes: four counts and three durations, the longest of which is a
        // round-trip in milliseconds and cannot outrun the socket's own
        // five-minute recycle. render() truncates rather than overruns anyway,
        // and the host suite sweeps every capacity from 1.
        char ping[128];
        ping_.render(now, ping, sizeof ping);
        ESP_LOGI(kTag, "-- ping   : %s", ping);

        // THE LIVENESS SIGNAL'S OWN INTER-ARRIVAL, and it is a DIFFERENT
        // QUANTITY from the two lines above it (M5 stage D-A3, deliverable 3).
        //
        //   `-- ping`'s worst/run   our client ping out, server pong back
        //   `-- feed`'s worst_gap   silence between BOOK EVENTS
        //   this line               silence between LIVENESS SIGNALS
        //
        // Three quantities, and until this stage two of them were called
        // `worst_gap` and the third was never printed at all. Named `-- signal`
        // rather than a fourth `worst` so a bench reader cannot mistake it for a
        // round-trip.
        //
        // `>=2x med` IS D-C'S FIRST NAMED CHECK, printed as a count rather than
        // left to arithmetic: stage C derived this venue's multiplier as 2.0 and
        // its falsifier is *any interval reaching 2 x median on a healthy
        // socket*. `PingScale::kFirstLong` is the 40 s bucket, so this number is
        // the falsifier's own count and a non-zero value on a healthy socket
        // raises k. Healthy intervals only -- see LivenessWatchdog::on_liveness
        // for why an outage-spanning gap is excluded.
        const auto& iv = lw.intervals();
        char sig[160];
        iv.render(sig, sizeof sig);
        ESP_LOGI(kTag,
                 "-- signal : %.*s n=%u max=%u ms >=2x med=%u | median %u ms"
                 " threshold %u ms %s",
                 static_cast<int>(venue::kLivenessSignal.size()),
                 venue::kLivenessSignal.data(),
                 static_cast<unsigned>(iv.total()),
                 static_cast<unsigned>(iv.worst_us() / 1000u),
                 static_cast<unsigned>(iv.count_from(PingScale::kFirstLong)),
                 static_cast<unsigned>(lw.median_ms()),
                 static_cast<unsigned>(lw.threshold_ms()),
                 lw.calibrated() ? "CALIBRATED" : "UNCALIBRATED");
        ESP_LOGI(kTag, "-- signals: %s", sig);
    }

    prev_ = cur;
    have_prev_ = true;
}

}  // namespace depthcharge::fw
