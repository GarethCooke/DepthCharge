// firmware/src/ws_supervisor.hpp — when to stop waiting for a socket, and what
// the reconnect constants have to be for that decision to be safe.
//
// ESP-IDF-free on purpose, like frame_reassembler.hpp, gap_histogram.hpp and
// stall_probe.hpp: this is arithmetic over three inputs (a clock, "is the socket
// up", "is the station associated") and it decides how long the panel stays
// grey. That is exactly the kind of thing that must be argued on the desk with
// ctest rather than by flashing a board and stopping a server by hand — which is
// how every reconnect change before this one was evaluated, at roughly one
// sample per evening.
//
// WsTransport owns the platform half: which client handle to open, the DNS warm,
// the logging. This owns the *policy* and the constants, and the constants are
// the interesting part, because two of them are not ours.
#pragma once

#include <cstdint>

namespace depthcharge::fw {

// =============================================================================
// What the shipped library does. Measured, not assumed.
// =============================================================================
//
// `esp_websocket_client` in Arduino-ESP32 2.0.14 is a precompiled archive, so
// these two numbers were read out of it with xtensa objdump rather than from a
// version of the IDF source that may or may not be the one that was built:
//
//   .literal.esp_websocket_client_init + 0x30 = 0x2710 = 10000
//       `client->wait_timeout_ms = WEBSOCKET_RECONNECT_TIMEOUT_MS` (10 s).
//
//   .text.esp_websocket_client_task + 0x3b5:
//       bnei a5, 3, ...          ; if (state != WEBSOCKET_STATE_WAIT_TIMEOUT)
//       l32i.n a5, a2, 56        ; wait_timeout_ms
//       srai   a10, ..., 1       ; / 2
//       callx8 <vTaskDelay>      ; vTaskDelay(wait_timeout_ms / 2) == 5000 ticks
//
// So a socket that aborts puts the client's own task to sleep for FIVE SECONDS
// before it looks at anything again, and at a 1 kHz tick those are milliseconds.
//
// This is the whole reason the file exists. `esp_websocket_client_stop()` sets
// `client->run = false` and then blocks on
// `xEventGroupWaitBits(STOPPED_BIT, ..., portMAX_DELAY)` — the task cannot
// observe the flag until it wakes, so stop() blocks for whatever is LEFT of that
// 5 s. Our own reconnect backoff is spent inside the same 5 s window, which
// makes the two anti-correlated and the sum a constant:
//
//   2026-08-09 bench, run C:  backoff 2445 ms + blocked in stop() 2545 ms
//                             = 4990 ms, against the 5000 ms above.
//   grey = 5000 + 4018 (connect) + 435 (Anvil's snapshot) = 9453 ms,
//   against the panel's own `grey for 9451 ms`.
//
// **Shortening the backoff therefore buys nothing.** It moves time out of a
// vTaskDelay and into a blocking call on loopTask, and the panel greys for
// exactly as long. That is the M3 residual brief's Part 2 premise, and it is
// false; what replaces it is that the transport keeps a second, already-built
// client handle and simply opens that one, leaving the sleeper to expire on its
// own clock instead of waiting for it. See ws_transport.hpp.
inline constexpr std::int64_t kClientWaitTimeoutMs = 10000;

// How long after its socket aborts the library's task takes to go away, given
// `disable_auto_reconnect = true` — which the transport now sets, so the task
// sets `run = false` on its first WAIT_TIMEOUT pass and exits after exactly one
// of these sleeps rather than looping forever waiting to reconnect itself.
// (`.text.esp_websocket_client_task + 0x36e` loads `config->auto_reconnect` and
// branches to the `run = false` store; `.text.esp_websocket_client_init + 0x535`
// is the `auto_reconnect = !config->disable_auto_reconnect` that feeds it.)
//
// A handle is reusable once that has happened, and not before:
// `esp_websocket_client_start()` returns ESP_FAIL while `state >= INIT`.
inline constexpr std::int64_t kClientSelfExitUs = (kClientWaitTimeoutMs / 2) * 1000;

// =============================================================================
// Ours.
// =============================================================================

// How often the transport is expected to call poll(). Not enforced here — it is
// loopTask's vTaskDelay — but the backoff is stated in terms of it, so it would
// be a lie to leave it implicit.
inline constexpr std::int64_t kSupervisePeriodUs = 250 * 1000;

// How long after the feed dies the first reconnect attempt goes out.
//
// This used to be 2 s, and used to be free: it was spent inside the library's
// 5 s sleep, so it cost the panel nothing. Now that the transport opens a spare
// handle instead of waiting for the sleeper, every microsecond of it is grey
// panel, so it is one poll period — the pass after the one that noticed.
//
// Not zero. The pass that first sees the socket down only records when it went
// down (there is no earlier timestamp to use), so one period is what the shape
// of the poll loop costs anyway; spelling it as the backoff makes the arithmetic
// in the tests match the arithmetic on the board. The real "don't retry into a
// radio that has not rejoined" protection is the association gate below, which
// is a condition rather than a delay and so costs nothing when it is satisfied.
inline constexpr std::int64_t kReconnectBackoffUs = kSupervisePeriodUs;

// Measured, 2026-08-09 bench: restart -> panel LIVE again, end to end, 6136 ms.
//
// Run C later split that figure — ~2545 ms of it was the blocking stop() this
// design no longer performs, 4018 ms was DNS + TCP + TLS + upgrade, ~435 ms was
// Anvil's snapshot — so the quantity kHandshakeBudgetUs actually has to cover is
// now the 4018 ms, and 6136 ms is a strict over-estimate of it. Kept as the
// anchor of the assert below anyway: it is one sample either way, and the cost
// of being wrong downward is a supervisor that kills connections a beat before
// they succeed, which is the one failure mode here that cannot self-correct.
inline constexpr std::int64_t kObservedRecoveryUs = 6136 * 1000;

// What one connection attempt is allowed to take before the supervisor is
// allowed to give up on it and open the other handle: DNS, TCP, the TLS
// handshake against the pinned root, and the HTTP upgrade.
//
// Understating it is how this code could turn a recoverable outage into a
// permanent one — hence the assert, and hence a value above the worst figure the
// bench has produced rather than the 2.5 s cold bring-up the first run suggested.
inline constexpr std::int64_t kHandshakeBudgetUs = 7 * 1000 * 1000;

static_assert(kHandshakeBudgetUs >= kObservedRecoveryUs,
              "the handshake budget must cover the slowest recovery the bench has measured");

// The interval between the START of one attempt and the start of the next, when
// the first one neither succeeded nor reported anything. Backoff plus budget, so
// that an attempt of ours is never disturbed inside its own budget: restarting a
// client mid-handshake is the single way this policy could make an outage
// permanent, and it is ruled out on a clock we own rather than on anything the
// library says.
inline constexpr std::int64_t kRetryCycleUs = kReconnectBackoffUs + kHandshakeBudgetUs;

static_assert(kRetryCycleUs > kHandshakeBudgetUs,
              "supervisor grace must exceed a full client reconnect or it preempts one");

// The two-handle invariant, and the reason it is stated as a sum rather than
// eyeballed. The transport alternates between handle A and handle B, so the
// tightest case is a failed attempt: the feed dies at T (handle A aborts, and A
// becomes reusable at T + kClientSelfExitUs); we open B at T + backoff; B fails
// almost at once; the next attempt falls at T + backoff + kRetryCycleUs and
// wants A back. If that lands before A has exited, `esp_websocket_client_start`
// refuses it and the outage costs an extra cycle for nothing.
static_assert(kReconnectBackoffUs + kRetryCycleUs > kClientSelfExitUs,
              "a retry must not come back to a handle whose task is still sleeping");

// =============================================================================
// The policy.
// =============================================================================

enum class SupervisorAction : std::uint8_t {
    None,
    // The socket came up. `elapsed_us` is how long the attempt took, which is
    // the measurement kHandshakeBudgetUs is guessed from — printed on every
    // recovery so the constant stops being a guess.
    ReportConnected,
    // Open the other client handle. `elapsed_us` is how long the feed has been
    // down, `attempt` is the ordinal of the attempt that is now in flight.
    StartAttempt,
};

struct SupervisorDecision {
    SupervisorAction action = SupervisorAction::None;
    std::int64_t elapsed_us = 0;
    std::uint32_t attempt = 0;
    // Set exactly once per outage, on the first poll where an attempt was due
    // and the station was not associated. One line in the log, not one every
    // 250 ms for as long as the Wi-Fi is out.
    bool wifi_holdoff = false;
};

struct SupervisorInput {
    std::int64_t now_us = 0;
    bool socket_connected = false;
    // Station associated with an AP and holding an IP. The gate exists because
    // the drops this was written for are the AP steering the board off one mesh
    // node and onto another: the socket dies, and for the next second or two
    // there is no route for DNS or TCP to travel over. An attempt fired into
    // that window cannot succeed, and it is not free — it burns a handle for
    // kClientSelfExitUs and a retry cycle for kRetryCycleUs.
    bool wifi_associated = false;
};

class WsSupervisor {
public:
    // One pass. Call it every kSupervisePeriodUs from a normal task context.
    //
    // The attempt clock is stamped HERE, when StartAttempt is returned, rather
    // than by the caller after the platform call comes back. That ordering used
    // to matter and no longer does: it existed because
    // `esp_websocket_client_stop()` blocked for up to 2.5 s before an attempt
    // could even begin, and letting a slow teardown eat the handshake budget put
    // the next restart on top of the current one. The transport does not call
    // stop() any more, so the only thing between this decision and the socket
    // being built is an xTaskCreate — and stamping early is the conservative
    // direction anyway.
    SupervisorDecision poll(const SupervisorInput& in) noexcept {
        SupervisorDecision out;

        if (in.socket_connected) {
            if (attempt_in_flight_) {
                out.action = SupervisorAction::ReportConnected;
                out.elapsed_us = in.now_us - attempt_started_us_;
                out.attempt = attempts_;
                attempt_in_flight_ = false;
            }
            outage_open_ = false;
            wifi_holdoff_reported_ = false;
            return out;
        }

        // First pass of an outage: there is no earlier timestamp than this one,
        // so this pass can only record when it noticed. The backoff is measured
        // from here.
        if (!outage_open_) {
            outage_open_ = true;
            disconnected_since_us_ = in.now_us;
            return out;
        }

        // The first attempt of an outage falls a backoff after the feed died;
        // every later one falls a full cycle after the previous attempt STARTED.
        const std::int64_t due = attempt_in_flight_
                                     ? attempt_started_us_ + kRetryCycleUs
                                     : disconnected_since_us_ + kReconnectBackoffUs;
        if (in.now_us < due) { return out; }

        if (!in.wifi_associated) {
            // Held, not skipped: `due` is not advanced, so the attempt goes out
            // on the first pass after the station is back rather than a cycle
            // later. That is the difference between a 4 s grey and an 11 s one
            // on exactly the drop this brief is about.
            if (!wifi_holdoff_reported_) {
                wifi_holdoff_reported_ = true;
                out.wifi_holdoff = true;
            }
            return out;
        }

        out.elapsed_us = in.now_us - disconnected_since_us_;
        note_attempt_begun(in.now_us);
        out.action = SupervisorAction::StartAttempt;
        out.attempt = attempts_;
        return out;
    }

    // The boot connection is attempt #1, and marking it is not bookkeeping — it
    // is what buys it the same handshake immunity every retry gets. Without it
    // the first poll sees a client that has simply never been connected, cannot
    // tell that from one that dropped, and opens the spare a backoff in: straight
    // through the cold TLS handshake, which the 2026-08-09 18:15 bench caught it
    // doing and which read as noise because it recovered.
    void note_attempt_begun(std::int64_t now_us) noexcept {
        ++attempts_;
        attempt_started_us_ = now_us;
        attempt_in_flight_ = true;
    }

    std::uint32_t attempts() const noexcept { return attempts_; }
    bool attempt_in_flight() const noexcept { return attempt_in_flight_; }

private:
    // Two clocks, and the whole correctness argument is the gap between them.
    // `disconnected_since_us_` says when the feed died and schedules the FIRST
    // attempt a backoff later. `attempt_started_us_` says when the last attempt
    // began and buys it kHandshakeBudgetUs of immunity, inside which poll() does
    // nothing at all.
    //
    // Each carries its own validity flag rather than reserving 0 for "unset",
    // and that is not tidiness. The old code used the 0 sentinel, and the boot
    // connection is stamped from `esp_timer_get_time()` — a clock that reads 0
    // at reset. On the board the call lands a second or two in, so it never bit;
    // on the desk the first test written against this file produced `t = 0`,
    // read it as "no attempt in flight" and opened a second connection straight
    // through the cold TLS handshake — the exact bug the boot stamp was added to
    // fix, re-entering by the back door of an encoding.
    std::int64_t disconnected_since_us_ = 0;
    std::int64_t attempt_started_us_ = 0;
    std::uint32_t attempts_ = 0;
    bool outage_open_ = false;
    bool attempt_in_flight_ = false;
    bool wifi_holdoff_reported_ = false;
};

// =============================================================================
// The association, which nothing was supervising at all.
// =============================================================================
//
// WsSupervisor above holds an attempt while the station is down and waits for it
// to come back. The 2026-08-10 bench found the case where it never does, and the
// panel stayed grey for the rest of the run:
//
//   Reason: 1 - UNSPECIFIED      the AP deauthenticates (a Deco block)
//   Reason: 202 - AUTH_FAIL      Arduino's one retry, rejected
//   reconnect due but the station is not associated — holding
//   ... nothing, forever. connects=2, rate 0.00/s, drew 0.
//
// THE CAUSE IS IN THE FRAMEWORK, READ OUT OF THE SHIPPED SOURCE.
// `WiFiGeneric.cpp:1083` only calls `WiFi.begin()` again when
// `_isReconnectableReason(reason)` is true, and that list (line 1177) contains
// `WIFI_REASON_802_1X_AUTH_FAILED` but NOT `WIFI_REASON_AUTH_FAIL` (202). The
// deauth reason 1 IS reconnectable, so Arduino retries exactly once; the AP is
// still blocking, so that attempt returns 202; `first_connect` is already false;
// `DoReconnect` stays false. Nothing calls `WiFi.begin()` ever again.
//
// So `connect_wifi()` being a one-shot at boot was a latent hole, and this is
// the same lesson the reconnect rework already produced, applied one layer down:
// SUPERVISE ON OBSERVED STATE, NOT ON THE LIBRARY'S POLICY. The observed state
// here is `WiFi.status() == WL_CONNECTED`, and if it has been false for long
// enough we stop waiting for Arduino and rejoin ourselves.
//
// Eighth time this precompiled vintage has decided a design, after
// esp_crt_bundle, heap_trace_start, reconnect_timeout_ms,
// CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS, the blocking stop(), the
// self-leaking setupDMA() and the S3's non-blocking buffer flip.

// How long the station may be down before we take the rejoin over.
//
// Arduino's own one-shot retry is fast — on the bench the deauth and the failed
// retry were 28 ms apart — so this only has to be clear of it, not of a full
// association. Five seconds is also longer than the ~2.5 s cold bring-up the
// first bench run measured, so a genuine roam that Arduino IS handling completes
// without us interrupting it.
inline constexpr std::int64_t kWifiRejoinAfterUs = 5 * 1000 * 1000;

// How often we retry once it is ours.
//
// FIVE SECONDS, NOT TEN, AND THE BENCH MOVED IT. The first version used ten on
// the reasoning that an association is scan + auth + assoc + DHCP and the boot
// one measured ~2.5 s, so ten was "a full attempt plus margin". That reasoning
// only considered the attempt. It ignored the term that actually dominates once
// the AP comes back: **the wait for the next attempt to be due.**
//
// The 2026-08-10 run made it visible — the rejoin that finally succeeded was
// #15, and from the AP being unblocked to the panel going live measured 5-10 s,
// of which the connect itself was 4,068 ms and the rest was this constant. Five
// seconds still leaves 2x a measured association, halves the worst-case wait, and
// costs nothing extra against an AP that is deliberately refusing us: a failed
// association is a scan, not a handshake, and there is no handle to burn — which
// is what makes this cheaper to retry than the socket cadence it sits under.
inline constexpr std::int64_t kWifiRejoinCycleUs = 5 * 1000 * 1000;

// Cold Wi-Fi association plus TLS, measured at the stage C bench: 2.5 s. It is a
// strict OVER-estimate of the association alone, which is the quantity that
// matters here, and that is the safe direction.
inline constexpr std::int64_t kObservedAssociationUs = 2500 * 1000;

// The real constraint on the retry cadence, and it took the assert firing to
// state it correctly. The first version asserted
// `kWifiRejoinCycleUs > kWifiRejoinAfterUs`, which is meaningless: the two are
// measured from different instants — the threshold from when the station went
// down, the cycle from the last rejoin — and neither bounds the other. Dropping
// the cycle to 5 s made them equal and the assert failed, which is the only
// reason anyone looked at what it was claiming.
//
// What must hold is that a retry does not land on top of an association that is
// still in flight, exactly as kRetryCycleUs must not preempt a handshake above.
static_assert(kWifiRejoinCycleUs >= kObservedAssociationUs,
              "a rejoin must not preempt an association that could still succeed");

// And the cadence when the last attempt has DEFINITIVELY failed, which is a
// different question and was costing most of the recovery.
//
// kWifiRejoinCycleUs is sized so a retry cannot land on an association that is
// still in flight. But an AP that is refusing us does not leave one in flight —
// the 2026-08-10 bench measured the refusal coming back in **60 ms**:
//
//   23:02:10.419  rejoining (#1)
//   23:02:10.473  Reason: 202 - AUTH_FAIL
//
// So for 4.9 of every 5 s we were waiting out a budget for an attempt that had
// already been over for a moment, and when the AP finally opened we noticed up
// to a full cycle late. That was the single largest tunable term left in the
// recovery: 25.6 s of grey, of which 4,220 ms was the TLS connect (the
// network's, and untouchable from here) and up to 5 s was this.
//
// Arduino latches the failure as WL_CONNECT_FAILED (WiFiGeneric.cpp:1065 on
// AUTH_FAIL, :1088 on ASSOC_FAIL), so "the attempt is over and it lost" is
// observable rather than inferred — the same principle as supervising the socket
// on its state instead of on the library's events. One second is 16x the
// measured refusal, so it cannot outrun a real answer.
inline constexpr std::int64_t kWifiRefusedRetryUs = 1000 * 1000;

static_assert(kWifiRefusedRetryUs < kWifiRejoinCycleUs,
              "the refused-retry path exists to be faster than the in-flight one");

class WifiSupervisor {
public:
    struct Decision {
        // Call WiFi.disconnect() then WiFi.begin(ssid, password).
        bool rejoin = false;
        // How long the station has been down, for the log line. The number is
        // the point: an association that comes back on its own never reaches
        // kWifiRejoinAfterUs, so a non-empty rejoin log IS the evidence that
        // the framework gave up.
        std::int64_t down_us = 0;
        std::uint32_t attempt = 0;
    };

    // `attempt_failed` is the station reporting that its last association
    // attempt is over and lost — Arduino's WL_CONNECT_FAILED. It is not the same
    // as "not associated", which is also true while an attempt is running, and
    // the difference is worth up to a whole cycle of grey panel.
    Decision poll(std::int64_t now_us, bool associated, bool attempt_failed = false) noexcept {
        Decision out;

        if (associated) {
            down_open_ = false;
            rejoins_ = 0;
            return out;
        }

        // Same shape as the outage clock above: the first pass that sees the
        // station down has no earlier timestamp than its own.
        if (!down_open_) {
            down_open_ = true;
            down_since_us_ = now_us;
            return out;
        }

        // A refused attempt is finished, so there is nothing to preempt and no
        // reason to sit out the in-flight budget. Only after we have actually
        // made an attempt of our own: before that, WL_CONNECT_FAILED is left
        // over from the framework's own retry and says nothing about ours.
        const std::int64_t retry_after =
            (attempt_failed && rejoins_ > 0) ? kWifiRefusedRetryUs : kWifiRejoinCycleUs;
        const std::int64_t due = (rejoins_ == 0) ? down_since_us_ + kWifiRejoinAfterUs
                                                 : last_rejoin_us_ + retry_after;
        if (now_us < due) { return out; }

        ++rejoins_;
        last_rejoin_us_ = now_us;
        out.rejoin = true;
        out.down_us = now_us - down_since_us_;
        out.attempt = rejoins_;
        return out;
    }

    std::uint32_t rejoins() const noexcept { return rejoins_; }

private:
    std::int64_t down_since_us_ = 0;
    std::int64_t last_rejoin_us_ = 0;
    std::uint32_t rejoins_ = 0;
    bool down_open_ = false;
};

}  // namespace depthcharge::fw
