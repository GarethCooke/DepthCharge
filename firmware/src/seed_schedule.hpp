// firmware/src/seed_schedule.hpp — WHEN the board asks Binance for a seed.
//
// M5 stage D-A2. ESP-IDF-free and host-tested, and the split is `resync.hpp`'s
// rather than a new idea: the POLICY that decides when a network action happens
// is arithmetic a desk can check, and the action itself is four esp-tls calls a
// desk cannot. `ws_supervisor.hpp` makes the same split for the same reason and
// says so.
//
// ===========================================================================
// WHY THIS FILE IS THE HIGHEST-VALUE TEST TARGET IN THE STAGE
// ===========================================================================
//
// Everything else here fails safe. If the transport is wrong the fetch fails,
// the adapter stays unseeded and the panel stays honestly grey — bad, visible,
// recoverable. **If THIS is wrong the board asks too often, and Binance bans
// the IP.** `/api/v3/depth?limit=1000` costs **50 weight** against a
// **6,000/minute** budget; the venue answers a breach with `429`, and a client
// that ignores `429` gets `418` and then a ban that no amount of grey-panel
// honesty recovers. That is the one failure in this milestone which is not
// reversible from the desk, and it is pure arithmetic — so it is tested.
//
// ===========================================================================
// THE RETRY CYCLE IS MEASURED FROM THE ISSUE, NOT FROM THE FAILURE
// ===========================================================================
//
// Measuring a retry delay from when a fetch FAILED lets the cycle collapse: a
// fetch that fails instantly (no address, TLS init refused) retries after the
// delay, and the effective rate is set by how fast failures happen rather than
// by the policy. Measured from the ISSUE, the cycle is bounded no matter how
// the attempt ended — which is the shape `ws_supervisor.hpp`'s reconnect
// holdoff already uses, and the reason the deleted `kWifiRefusedRetryUs` fast
// path was wrong: *a measurement of the failing case does not bound the
// succeeding case.*
//
// The `static_assert` below is what stops the cycle from ever being shorter
// than the deadline it contains. A retry that could start while its own
// predecessor was still running would put two seeds in flight, two TLS sessions
// on a board sized for one spare, and two 50-weight requests where the policy
// intended one.
#pragma once

#include <cstddef>
#include <cstdint>

namespace depthcharge::fw {

// What the caller should do this pass.
enum class SeedAction : std::uint8_t {
    None = 0,
    Issue,     // start a fetch now
    Abandon,   // a fetch is in flight and must be torn down (deadline or overflow)
    GiveUp,    // stop asking until something changes; report once
};

// Why the schedule is not issuing, for the one line a bench greps.
enum class SeedHold : std::uint8_t {
    Nothing = 0,   // nothing wants a seed
    InFlight,      // one is running
    CoolingDown,   // inside the retry cycle
    SocketDown,    // the feed's socket is down; a fetch must not overlap a reconnect
    RateLimited,   // the venue said 429
    Banned,        // the venue said 418
    GaveUp,        // too many consecutive failures
};

// THE RETRY CYCLE. 20 s, and the floor is not a preference: it must exceed
// `kBinanceFetchDeadlineMs` (15,000) or a retry could begin inside the fetch it
// is retrying. The ceiling is the venue's patience with an unseeded client and
// the pre-seed buffer's horizon, both of which say sooner is better; 20 s is
// the smallest round number above the deadline.
//
// At worst this is 3 fetches/minute x 50 weight = **150 of 6,000**, i.e. 2.5%
// of the budget with every attempt failing. The succeeding case is far cheaper,
// because a satisfied seed stops asking.
inline constexpr std::int64_t kSeedRetryCycleUs = 20'000'000;

// The fetch's own deadline, in the same units. Named here rather than reached
// for through the engine header so this file stays free of `engine/`; the
// `static_assert` in `rest_fetch.hpp` ties the two spellings together.
inline constexpr std::int64_t kSeedDeadlineUs = 15'000'000;

static_assert(kSeedRetryCycleUs > kSeedDeadlineUs,
              "a retry must not be able to start inside the fetch it is retrying: two seeds "
              "in flight is two TLS sessions and two 50-weight requests");

// AFTER A 429. The venue is telling us the schedule is wrong, so the answer is
// not to try again on the same cadence — it is to back off by a lot and let the
// minute roll over. 90 s clears a 1-minute weight window with margin.
inline constexpr std::int64_t kSeedRateLimitBackoffUs = 90'000'000;

// HOW MANY CONSECUTIVE FAILURES BEFORE THE BOARD STOPS ASKING. Not a retry
// budget — a statement that something is wrong that retrying will not fix (no
// DNS, no route, a venue outage). Five at a 20 s cycle is ~100 s of trying.
//
// Giving up is NOT permanent: `note_socket_change()` re-arms it, because a new
// socket is new information and the commonest cause of five failures in a row
// is a link that has since come back.
inline constexpr std::uint32_t kSeedGiveUpAfter = 5;

struct SeedInput {
    bool wanted = false;            // adapter has no baseline, or asked for a re-seed
    bool in_flight = false;         // a fetch is running
    bool buffer_overflowed = false; // the pre-seed buffer overflowed under it
    // A SEED FETCH AND A TLS RECONNECT MUST NEVER OVERLAP. See the rule below.
    bool socket_up = false;
    std::int64_t now_us = 0;
};

// The policy. Holds no clock of its own: every decision is a function of the
// input and of the instants it was told about, which is what makes it testable.
class SeedSchedule {
public:
    SeedAction step(const SeedInput& in) noexcept {
        last_hold_ = SeedHold::Nothing;

        if (in.in_flight) {
            // THE SOCKET DROPPED UNDER THE FETCH: ABANDON, AND IT OUTRANKS
            // EVERY OTHER REASON TO KEEP GOING.
            //
            // Two independent grounds, and either alone would be enough.
            //
            // MEMORY. A reconnect rebuilds a TLS session, and mbedTLS is pinned
            // internal at two contiguous 16,717 B blocks. The board measured a
            // seed fetch taking `largest_during` down to **10,740 B** — already
            // below that threshold on its own. A fetch still holding its two
            // blocks while the transport tries to build a third is the one
            // arrangement guaranteed not to fit, and the failure would land on
            // the RECONNECT, which is the half that matters.
            //
            // CORRECTNESS. The seed is bracketed against the diff stream. A
            // socket drop means the stream this body was going to be bracketed
            // against has ended, so the body is worthless before it arrives:
            // the adapter drops its book on `Gap{Disconnect}` and the next
            // connect starts buffering from scratch. Finishing the transfer
            // would spend 50 IP weight and ~64 KB of downstream on a body that
            // cannot be adopted.
            if (!in.socket_up) { return SeedAction::Abandon; }
            if (in.buffer_overflowed) { return SeedAction::Abandon; }
            // ABANDON BEATS EVERYTHING. A fetch whose pre-seed buffer has
            // overflowed is already spent — `test_binance_adapter.cpp` measures
            // it: the body is adopted, fails its bracket against the zeroed
            // buffer and is dropped inside one call, so finishing the transfer
            // buys nothing and costs the socket and the deadline.
            if (in.now_us - issued_us_ >= kSeedDeadlineUs) { return SeedAction::Abandon; }
            last_hold_ = SeedHold::InFlight;
            return SeedAction::None;
        }

        // AND NOT STARTED WHILE THE SOCKET IS DOWN, which is the other half of
        // the same rule. A reconnect is exactly when internal SRAM is tightest
        // and exactly when a seed is most tempting (no book, `wanted` is true),
        // so without this the two would collide precisely when they are least
        // affordable. Checked before the cooling-off so the hold is REPORTED as
        // the socket rather than as the schedule.
        if (!in.socket_up) { last_hold_ = SeedHold::SocketDown; return SeedAction::None; }
        if (banned_) { last_hold_ = SeedHold::Banned; return SeedAction::None; }
        if (gave_up_) { last_hold_ = SeedHold::GaveUp; return SeedAction::None; }

        // LEVEL, NOT EDGE. `wanted` is polled every pass and never latched here,
        // because the adapter re-raises it from inside `drop_book` — an edge
        // would consume the request the drop just made and leave the board grey
        // over a healthy socket with nothing climbing. That is the M4 stage A2
        // defect, and ARCHITECTURE §9 (2026-08-20) is the row about it.
        if (!in.wanted) { return SeedAction::None; }

        // `have_issued_` AND NOT `issued_us_ != 0`, WHICH IS WHAT THE TESTS
        // CAUGHT. Using 0 as "never issued" makes a fetch issued AT 0 look like
        // no fetch at all — and `esp_timer_get_time()` starts near 0 at boot,
        // so the very first seed of every boot would have skipped its own
        // cooling-off and re-issued immediately. A sentinel that collides with
        // a legitimate value, in the one file whose job is to not ask too often.
        if (have_issued_ && in.now_us - issued_us_ < hold_off_us_) {
            last_hold_ = (hold_off_us_ > kSeedRetryCycleUs) ? SeedHold::RateLimited
                                                            : SeedHold::CoolingDown;
            return SeedAction::None;
        }

        if (consecutive_failures_ >= kSeedGiveUpAfter) {
            gave_up_ = true;
            last_hold_ = SeedHold::GaveUp;
            return SeedAction::GiveUp;   // returned ONCE; the latch reports after
        }
        return SeedAction::Issue;
    }

    void note_issued(std::int64_t now_us) noexcept {
        issued_us_ = now_us;
        have_issued_ = true;
        hold_off_us_ = kSeedRetryCycleUs;
        ++issued_;
    }

    // The venue answered. `status` is the HTTP status; 0 means the fetch never
    // got one (transport failure).
    void note_result(bool ok, int status, std::int64_t now_us) noexcept {
        (void)now_us;
        if (ok) {
            consecutive_failures_ = 0;
            hold_off_us_ = kSeedRetryCycleUs;
            return;
        }
        ++consecutive_failures_;
        if (status == 418) {
            // The venue's answer to a client that ignored 429. Stop, and make
            // the operator's next act deliberate.
            banned_ = true;
            return;
        }
        if (status == 429) {
            ++rate_limited_;
            hold_off_us_ = kSeedRateLimitBackoffUs;
            return;
        }
        hold_off_us_ = kSeedRetryCycleUs;
    }

    // A new socket is new information: it clears a give-up, because the
    // commonest cause of five consecutive failures is a link that has since
    // come back. It deliberately does NOT clear `banned_` — a 418 is the venue's
    // decision about this IP and reconnecting does not change its mind.
    void note_socket_change() noexcept {
        consecutive_failures_ = 0;
        gave_up_ = false;
        issued_us_ = 0;
        have_issued_ = false;
        hold_off_us_ = kSeedRetryCycleUs;
    }

    // How long until this schedule wants to be asked again: 0 = now, >0 = in
    // that many microseconds, -1 = never (nothing wants a seed, or we have
    // stopped). This is what the feed task folds into its wait, so a wrong
    // answer here is a feed task that sleeps through its own fetch.
    std::int64_t due_in_us(const SeedInput& in) const noexcept {
        if (in.in_flight) {
            if (!in.socket_up) { return 0; }   // abandon now, not at the deadline
            const std::int64_t left = kSeedDeadlineUs - (in.now_us - issued_us_);
            return left > 0 ? left : 0;
        }
        if (banned_ || gave_up_ || !in.wanted || !in.socket_up) { return -1; }
        if (!have_issued_) { return 0; }
        const std::int64_t left = hold_off_us_ - (in.now_us - issued_us_);
        return left > 0 ? left : 0;
    }

    SeedHold hold() const noexcept { return last_hold_; }
    std::uint32_t issued() const noexcept { return issued_; }
    std::uint32_t rate_limited() const noexcept { return rate_limited_; }
    std::uint32_t consecutive_failures() const noexcept { return consecutive_failures_; }
    bool banned() const noexcept { return banned_; }
    bool gave_up() const noexcept { return gave_up_; }

private:
    std::int64_t issued_us_ = 0;
    bool have_issued_ = false;
    std::int64_t hold_off_us_ = kSeedRetryCycleUs;
    std::uint32_t issued_ = 0;
    std::uint32_t rate_limited_ = 0;
    std::uint32_t consecutive_failures_ = 0;
    bool banned_ = false;
    bool gave_up_ = false;
    SeedHold last_hold_ = SeedHold::Nothing;
};

inline const char* seed_hold_name(SeedHold h) noexcept {
    switch (h) {
        case SeedHold::Nothing:     return "nothing-wanted";
        case SeedHold::InFlight:    return "in-flight";
        case SeedHold::CoolingDown: return "cooling-down";
        case SeedHold::SocketDown:  return "socket-down";
        case SeedHold::RateLimited: return "RATE-LIMITED";
        case SeedHold::Banned:      return "BANNED(418)";
        case SeedHold::GaveUp:      return "gave-up";
    }
    return "?";
}

inline const char* seed_action_name(SeedAction a) noexcept {
    switch (a) {
        case SeedAction::None:    return "none";
        case SeedAction::Issue:   return "issue";
        case SeedAction::Abandon: return "abandon";
        case SeedAction::GiveUp:  return "give-up";
    }
    return "?";
}

}  // namespace depthcharge::fw
