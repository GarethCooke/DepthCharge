// firmware/src/ws_transport.cpp — see ws_transport.hpp.
#include "ws_transport.hpp"

#include <WiFi.h>

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"

#if DC_OWNED_WS
#include <strings.h>   // strncasecmp, for the upgrade's header check

#include "lwip/sockets.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#endif

#include "anvil_root_ca.hpp"

namespace depthcharge::fw {
namespace {
constexpr const char* kTag = "ws";
#if DC_OWNED_WS
// How long esp_tls may spend on DNS + TCP + the handshake before it gives up.
// The bench's worst measured connect is 4,220 ms, and kHandshakeBudgetUs (7 s)
// is what the supervisor allows an attempt — so this sits between them: long
// enough never to abandon a connect that would have succeeded, short enough that
// esp-tls returns before the supervisor would have wanted to try again.
constexpr std::uint32_t kConnectTimeoutMs = 6000;

// How long the 101 may take to arrive once the request is written.
constexpr std::int64_t kUpgradeBudgetUs = 5 * 1000 * 1000;

// How long a single write may take before the socket is treated as dead. A WALL
// CLOCK, not a stall count, and the 2026-08-15 review is why: the first version
// counted 100 WANT_WRITE stalls at 10 ms apart and called that "a second" — but
// each esp_tls_conn_write can itself block up to the 5 s SO_SNDTIMEO before
// reporting the stall, so the "bounded" loop's true worst case was ~8.4 minutes
// of the RX task held hostage by a wedged peer, with the supervisor unable to
// help because this IS the task it asks for sockets. Two seconds is still an
// age for a 6-byte pong; one in-flight SO_SNDTIMEO can extend the true bound by
// up to 5 s, which is accepted and stated rather than hidden.
constexpr std::int64_t kWriteBudgetUs = 2 * 1000 * 1000;

// Case-insensitive header lookup over a NUL-terminated header block. HTTP field
// names are case-insensitive (RFC 9110 §5.1) and servers differ; a check that
// only matched one spelling would silently degrade to no check at all.
//
// Returns true only if the field is present AND begins with `want`. Present and
// different is false, which is the case that matters: it means something
// answered the upgrade without doing the SHA1.
bool header_begins_with(const char* headers, const char* name, const char* want) noexcept {
    const std::size_t name_len = std::strlen(name);
    const std::size_t want_len = std::strlen(want);
    for (const char* line = headers; line != nullptr && *line != '\0';) {
        const char* eol = std::strstr(line, "\r\n");
        const std::size_t len = (eol != nullptr) ? static_cast<std::size_t>(eol - line)
                                                 : std::strlen(line);
        if (len > name_len && strncasecmp(line, name, name_len) == 0) {
            const char* v = line + name_len;
            const char* end = line + len;
            while (v < end && (*v == ' ' || *v == '\t')) { ++v; }
            return static_cast<std::size_t>(end - v) >= want_len &&
                   std::strncmp(v, want, want_len) == 0;
        }
        line = (eol != nullptr) ? eol + 2 : nullptr;
    }
    return false;
}
#else
// A/B in the log, because "handle 1" and "attempt #1" beside each other at a
// bench at 11pm is one transposition away from a wrong diagnosis.
constexpr char kHandleName[] = {'A', 'B'};
#endif
}  // namespace

bool WsTransport::pick_strongest(const char* ssid, std::uint8_t (&bssid)[6],
                                 std::int32_t& channel) noexcept {
    const std::uint32_t t0 = millis();
    const std::int16_t n = WiFi.scanNetworks();
    if (n <= 0) {
        ESP_LOGW(kTag, "wifi: scan returned %d after %u ms — letting the driver choose",
                 static_cast<int>(n), static_cast<unsigned>(millis() - t0));
        WiFi.scanDelete();
        return false;
    }

    // EVERY sibling is printed, not just the winner, and that is the point: the
    // acceptance check is "did this boot join the strongest node IT could see",
    // which is unanswerable from a log that only records the choice. It is also
    // the co-channel picture for free — three Decos on channel 4 is the
    // 2026-08-13 desk, and worth seeing when the numbers stop making sense.
    std::int32_t best_rssi = 0;
    std::uint32_t siblings = 0;
    for (std::int16_t i = 0; i < n; ++i) {
        if (WiFi.SSID(i) != ssid) { continue; }
        const std::uint8_t* b = WiFi.BSSID(i);
        const std::int32_t rssi = WiFi.RSSI(i);
        const std::int32_t ch = WiFi.channel(i);
        ESP_LOGI(kTag, "wifi: sibling %02X:%02X:%02X:%02X:%02X:%02X ch %d rssi %d dBm", b[0], b[1],
                 b[2], b[3], b[4], b[5], static_cast<int>(ch), static_cast<int>(rssi));
        if (siblings == 0 || rssi > best_rssi) {
            std::memcpy(bssid, b, sizeof(bssid));
            channel = ch;
            best_rssi = rssi;
        }
        ++siblings;
    }
    WiFi.scanDelete();

    if (siblings == 0) {
        ESP_LOGW(kTag, "wifi: %d networks visible, none of them ours — letting the driver choose",
                 static_cast<int>(n));
        return false;
    }
    ESP_LOGI(kTag, "wifi: joining %02X:%02X:%02X:%02X:%02X:%02X ch %d at %d dBm — strongest of %u"
                   " siblings, scan took %u ms",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
             static_cast<int>(channel), static_cast<int>(best_rssi),
             static_cast<unsigned>(siblings), static_cast<unsigned>(millis() - t0));
    return true;
}

bool WsTransport::connect_wifi(const char* ssid, const char* password,
                               std::uint32_t timeout_ms) noexcept {
    // Kept so supervise() can rejoin later. This function used to be the ONLY
    // place the station was ever asked to associate, which is the hole the
    // 2026-08-10 bench fell into — see WifiSupervisor.
    ssid_ = ssid;
    password_ = password;

    WiFi.mode(WIFI_STA);
    // Scan every channel and join by SIGNAL, not by whoever answers first — the
    // framework default is WIFI_FAST_SCAN, first probe response wins, which on a
    // mesh where every node broadcasts the same SSID is a lottery.
    //
    // THESE TWO LINES ARE NO LONGER THE FIX; they are belt and braces. They were
    // the fix on 2026-08-13 and failed their five-boot acceptance the same
    // evening — two of five boots landed on weak siblings with both settings
    // demonstrably active (`hardware/bench-2026-08-13-wifi-drop-diagnosis.md`,
    // closing section). The boot join is now explicit (pick_strongest below);
    // what these still cover is every path that reaches a plain WiFi.begin(),
    // which is the supervisor's rejoin, where a blocking scan is not affordable.
    // Ordered before begin() like setSleep(), and for the same reason: applied
    // at STA start, so it must be cached before the association is queued.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    // Modem sleep off by default. The power-save mode parks the radio between
    // DTIM beacons and can add hundreds of milliseconds of RX latency — which on
    // a feed whose worst healthy inter-frame gap is 391-594 ms against a 1000 ms
    // watchdog would manufacture outages that never happened. This is a
    // mains-powered desk object; the power cost is irrelevant and the latency is
    // not.
    //
    // Called AFTER WiFi.mode() and BEFORE WiFi.begin(), which is the order that
    // makes it stick, and that is a claim from the shipped source rather than
    // from habit: `WiFiGenericClass::setSleep` only forwards to
    // `esp_wifi_set_ps` when `getMode() & WIFI_MODE_STA` is already set, so
    // calling it first would cache the preference and never apply it; and the
    // Arduino event handler re-applies the cached value on
    // ARDUINO_EVENT_WIFI_STA_START, so association cannot undo it either.
    WiFi.setSleep(kWifiPowerSave);

    // THE JOIN IS OURS, not the driver's. The two calls above were the
    // 2026-08-13 fix and they failed their own five-boot acceptance that
    // evening; pick_strongest() surveys and names the node instead, and the
    // survey it prints is the acceptance instrument. Falling back to a plain
    // begin() when the scan finds nothing is deliberate — a board that cannot
    // see its own SSID has a bigger problem than which sibling it picks, and
    // refusing to try would turn a bad scan into a dead panel.
    std::uint8_t bssid[6] = {};
    std::int32_t channel = 0;
    if (pick_strongest(ssid, bssid, channel)) {
        WiFi.begin(ssid, password, channel, bssid);
    } else {
        WiFi.begin(ssid, password);
    }

    const std::uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - started > timeout_ms) {
            ESP_LOGE(kTag, "wifi: no association after %u ms (status %d)",
                     static_cast<unsigned>(timeout_ms), static_cast<int>(WiFi.status()));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Read the mode back OUT OF THE DRIVER rather than reporting what we asked
    // for. `WiFi.getSleep()` would only return Arduino's cached preference, and
    // the whole value of this line is that it cannot agree with us by
    // construction: if a bench run shows arrival-side holes with `ps=0` printed
    // here, power save is excluded as a candidate on evidence rather than on the
    // presence of a `setSleep` call three lines up.
    wifi_ps_type_t ps = WIFI_PS_NONE;
    const esp_err_t ps_err = esp_wifi_get_ps(&ps);
    // bssid= names WHICH mesh node this association landed on — every Deco
    // broadcasts the same SSID, so rssi without bssid is unattributable, and
    // the 2026-08-13 storm was undiagnosable for exactly that reason: −78 dBm
    // could not say "far node" without the node's address beside it. Boot-time
    // only; a mid-run re-association is not reprinted here.
    ESP_LOGI(kTag, "wifi up: ip=%s bssid=%s ch=%d rssi=%d dBm | power-save requested=%s"
                   " driver ps=%d%s (0=NONE 1=MIN_MODEM 2=MAX_MODEM)",
             WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str(),
             static_cast<int>(WiFi.channel()), static_cast<int>(WiFi.RSSI()),
             kWifiPowerSave ? "ON" : "OFF", static_cast<int>(ps),
             (ps_err == ESP_OK) ? "" : " (read failed)");
    return true;
}

std::int64_t WsTransport::warm_dns() noexcept {
    const std::int64_t t0 = esp_timer_get_time();

    addrinfo hints = {};
    hints.ai_family = AF_INET;  // the client connects over IPv4; asking for both
                                // would time a lookup the socket never makes
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const int rc = ::getaddrinfo(kAnvilHost, kAnvilPortText, &hints, &res);
    const std::int64_t elapsed = esp_timer_get_time() - t0;
    if (res != nullptr) { ::freeaddrinfo(res); }

    if (rc != 0) {
        // Not fatal and not a reason to skip the attempt: the client resolves
        // again itself, and a resolver that failed here may succeed a beat
        // later. Logged because a reconnect that fails with `dns=fail` in front
        // of it is a different bug from one that fails after a good lookup.
        ESP_LOGW(kTag, "dns: %s did not resolve (rc %d) after %d ms", kAnvilHost, rc,
                 static_cast<int>(elapsed / 1000));
    }
    return elapsed;
}

#if !DC_OWNED_WS
bool WsTransport::start() noexcept {
    esp_websocket_client_config_t cfg = {};
    cfg.uri = kAnvilUri;
    cfg.buffer_size = kWsRxBufferBytes;

    // TLS trust anchor: a pinned root, because the certificate bundle is not
    // reachable through this vintage of esp_websocket_client_config_t. The full
    // reasoning, the measured chain and the verification are in anvil_root_ca.hpp.
    cfg.cert_pem = kAnvilRootCaPem;
    cfg.cert_len = 0;  // NUL-terminated PEM

    // No Origin header. ARCHITECTURE §7 closed this: M0 measured the deployed
    // upgrade accepting a client that sends none. If that ever changes, the
    // server rejects the upgrade loudly at connect and the one-line fallback is:
    //     cfg.headers = "Origin: https://anvil.garethcooke.com\r\n";

    // AUTO-RECONNECT OFF, which reverses the previous decision on new evidence.
    //
    // It was left on as a backstop, and on the bench it earned nothing: across
    // two outages it held the connection down for the full 12 s the supervisor
    // gave it and recovered neither. It is now actively harmful, because the
    // spare-handle design depends on a retired client GOING AWAY. With
    // auto-reconnect on, the sleeper wakes 10 s after its abort and opens a
    // second live socket to Anvil that nothing in this firmware is watching —
    // two sockets, one reassembler. With it off, the library's task takes the
    // `if (!client->config->auto_reconnect) { client->run = false; break; }`
    // arm of WEBSOCKET_STATE_WAIT_TIMEOUT and deletes itself, leaving the handle
    // clean for reuse. Read out of the shipped archive, not the docs:
    // `.text.esp_websocket_client_task + 0x36e`.
    //
    // The pingpong watchdog stays on: if the socket dies quietly the client
    // tears it down and tells us, which arrives here as DISCONNECTED and becomes
    // Gap{Disconnect}. Our own RX watchdog is the faster and more honest of the
    // two — it fires on the *data* stopping, not on the socket dying — but both
    // paths exist because a half-open TCP connection can outlive a feed.
    cfg.disable_auto_reconnect = true;
    // The experiment knob — see kWsPingPong in the header for why the OFF arm
    // needs BOTH the day-long interval and the explicit disable flag (zeroing
    // either field substitutes a default in this vintage).
    cfg.ping_interval_sec = kWsPingPong ? 10 : 86400;
    cfg.pingpong_timeout_sec = kWsPingPong ? 20 : 0;
    cfg.disable_pingpong_discon = !kWsPingPong;

    // The client creates its own task, which this IDF vintage gives no way to
    // pin (there is no task_core_id field). That is acceptable precisely because
    // this task does nothing but memcpy into a slot and post it: the engine work
    // is all on the Core 0 feed task. Give it enough stack for mbed-TLS.
    cfg.task_stack = 6144;
    cfg.task_prio = 5;

    // Both handles, from one config. `esp_websocket_client_init` strdup()s every
    // string it keeps, so the two do not alias this stack object or each other.
    for (std::size_t i = 0; i < kClientCount; ++i) {
        clients_[i] = esp_websocket_client_init(&cfg);
        if (clients_[i] == nullptr) {
            ESP_LOGE(kTag, "esp_websocket_client_init failed for handle %c", kHandleName[i]);
            return false;
        }
        if (esp_websocket_register_events(clients_[i], WEBSOCKET_EVENT_ANY,
                                          &WsTransport::event_trampoline, this) != ESP_OK) {
            ESP_LOGE(kTag, "esp_websocket_register_events failed for handle %c", kHandleName[i]);
            return false;
        }
    }

    live_.store(0, std::memory_order_relaxed);
    const std::int64_t dns_us = warm_dns();
    ESP_LOGI(kTag, "connecting to %s on handle A (dns %d ms)", kAnvilUri,
             static_cast<int>(dns_us / 1000));

    const esp_err_t err = esp_websocket_client_start(clients_[0]);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_websocket_client_start: %s", esp_err_to_name(err));
        return false;
    }

    // The boot connection is attempt #1, and marking it is not bookkeeping — it
    // is what buys it the same handshake immunity every retry gets. Without this
    // the supervisor sees a client that has simply never been connected, cannot
    // tell that from one that dropped, and opens the spare a backoff in: straight
    // through the cold TLS handshake, which the 2026-08-09 18:15 log caught it
    // doing (`attempt #2` for the first outage, and connects=2 before any outage
    // had happened). It recovered, so it read as noise rather than as a bug.
    supervisor_.note_attempt_begun(esp_timer_get_time());
    return true;
}
#endif  // !DC_OWNED_WS

void WsTransport::sample_rssi(std::int64_t now) noexcept {
    if (now - last_rssi_us_ < kRssiPeriodUs) { return; }
    last_rssi_us_ = now;
    // esp_wifi_sta_get_ap_info rather than WiFi.RSSI(): the Arduino wrapper
    // returns 0 both for "not associated" and for a genuine 0 dBm, and this is
    // an instrument whose whole job is to be checkable against a link verdict.
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        link_.note(static_cast<std::int8_t>(ap.rssi));
    }
}

#if !DC_OWNED_WS
bool WsTransport::open_spare(const SupervisorDecision& d) noexcept {
    const std::uint8_t current = live_.load(std::memory_order_relaxed);
    const std::uint8_t spare = static_cast<std::uint8_t>(current ^ 1u);

    // Warmed BEFORE `live_` moves, and the order matters for one reason: this
    // call can block for as long as the resolver takes, and for that whole time
    // `live_` must still name a handle whose state is true. Pointing it at a
    // handle that has not been started yet would make connected() answer for a
    // client that does not exist.
    //
    // It is also the honest place for the measurement — the last instant that
    // belongs to us rather than to the library.
    const std::int64_t dns_us = warm_dns();

    ESP_LOGW(kTag, "feed down %d ms — opening handle %c (attempt #%u, dns %d ms)",
             static_cast<int>(d.elapsed_us / 1000), kHandleName[spare],
             static_cast<unsigned>(d.attempt), static_cast<int>(dns_us / 1000));

    // Published first, started second. A handle that has not been started cannot
    // deliver anything, so this ordering is free; the reverse is not, because
    // the client's task begins running the instant start() returns and there is
    // no point at which a DATA chunk may arrive against a `live_` that still
    // names the handle being retired.
    live_.store(spare, std::memory_order_relaxed);

    const esp_err_t err = esp_websocket_client_start(clients_[spare]);
    if (err != ESP_OK) {
        // Almost always ESP_FAIL for "the client has started" — the spare's
        // previous task has not finished its 5 s sleep. The constants are
        // asserted to keep that from happening (ws_supervisor.hpp), so it is
        // worth an error line rather than a silent retry.
        ESP_LOGE(kTag, "handle %c would not start: %s", kHandleName[spare],
                 esp_err_to_name(err));
        live_.store(current, std::memory_order_relaxed);
        return false;
    }
    return true;
}
#endif  // !DC_OWNED_WS

void WsTransport::supervise() noexcept {
#if DC_OWNED_WS
    if (rx_task_ == nullptr) { return; }
#else
    if (clients_[0] == nullptr) { return; }
#endif

    const std::int64_t now = esp_timer_get_time();
    // Sampled on every pass, connected or not, and before anything below — the
    // rssi through a hole is exactly the number a link verdict has to be checked
    // against, so it must not stop being collected precisely when the transport
    // is unhappy.
    sample_rssi(now);

    SupervisorInput in;
    in.now_us = now;
    in.socket_connected = connected();
    // WiFi.status(), not esp_wifi_sta_get_ap_info(): the question this gate asks
    // is "could a TCP connection possibly succeed", and that needs an IP, which
    // is what Arduino's status tracks and an AP record does not.
    in.wifi_associated = (WiFi.status() == WL_CONNECTED);

    // THE ASSOCIATION, BEFORE THE SOCKET. Arduino gives up permanently on an
    // AUTH_FAIL deauth (ws_supervisor.hpp cites the framework lines), so if
    // nothing here rejoins, the socket supervisor below holds forever and the
    // panel greys for the rest of the run — which is exactly what the bench saw.
    //
    // WiFi.begin() is non-blocking: it queues the association and returns, so
    // this costs loopTask nothing and cannot stall the 250 ms poll. The
    // disconnect() first is what clears WL_CONNECT_FAILED, which the framework
    // latches on AUTH_FAIL and which begin() alone does not reset.
    // WL_CONNECT_FAILED is Arduino latching "that attempt is over and it lost"
    // (WiFiGeneric.cpp:1065 on AUTH_FAIL, :1088 on ASSOC_FAIL). Distinguishing
    // it from a plain "not associated" is what lets a refused retry come back in
    // a second instead of five — the bench measured the refusal itself at 60 ms.
    const bool wifi_refused = (WiFi.status() == WL_CONNECT_FAILED);
    if (const auto w = wifi_supervisor_.poll(now, in.wifi_associated, wifi_refused); w.rejoin) {
        ESP_LOGW(kTag, "wifi down %d ms and the framework has stopped trying — rejoining (#%u)",
                 static_cast<int>(w.down_us / 1000), static_cast<unsigned>(w.attempt));
        WiFi.disconnect();
        if (ssid_ != nullptr) {
            WiFi.begin(ssid_, password_);
        } else {
            // Only reachable if supervise() ran before connect_wifi(), which
            // main.cpp's bring-up order rules out. Reported rather than silently
            // skipped, because a rejoin that does nothing looks identical in the
            // log to one that failed.
            ESP_LOGE(kTag, "no credentials to rejoin with — connect_wifi() never ran");
        }
    }

    const SupervisorDecision d = supervisor_.poll(in);

    if (d.wifi_holdoff) {
        ESP_LOGW(kTag, "reconnect due but the station is not associated — holding");
    }

    switch (d.action) {
        case SupervisorAction::ReportConnected:
            // The measurement kHandshakeBudgetUs is guessed from, printed on
            // every recovery so the constant stops being a guess. This is the
            // SOCKET half only; the panel prints its own "grey for N ms", and
            // the difference between the two numbers is how long Anvil took to
            // send the snapshot that grants LIVE.
#if DC_OWNED_WS
            ESP_LOGI(kTag, "socket up, %d ms into attempt #%u",
                     static_cast<int>(d.elapsed_us / 1000), static_cast<unsigned>(d.attempt));
#else
            ESP_LOGI(kTag, "socket up on handle %c, %d ms into attempt #%u",
                     kHandleName[live_.load(std::memory_order_relaxed)],
                     static_cast<int>(d.elapsed_us / 1000),
                     static_cast<unsigned>(d.attempt));
#endif
            break;

        case SupervisorAction::StartAttempt:
#if DC_OWNED_WS
            // The decision is loopTask's; the work is the RX task's. Everything
            // that used to happen here — the DNS warm, esp_websocket_client_start
            // — blocked this task for as long as the network felt like it, and
            // this is now a single store. The attempt clock has already been
            // stamped inside the policy, so the ~4 s the RX task is about to
            // spend inside esp_tls_conn_new_sync is charged to the attempt that
            // asked for it rather than to the one after.
            ESP_LOGW(kTag, "feed down %d ms — asking the RX task for a socket (attempt #%u)",
                     static_cast<int>(d.elapsed_us / 1000), static_cast<unsigned>(d.attempt));
            connect_requested_.store(true, std::memory_order_relaxed);
#else
            (void)open_spare(d);
#endif
            break;

        case SupervisorAction::None:
            break;
    }
}

#if !DC_OWNED_WS
void WsTransport::event_trampoline(void* arg, esp_event_base_t /*base*/, std::int32_t id,
                                   void* event_data) noexcept {
    static_cast<WsTransport*>(arg)->on_event(
        id, static_cast<esp_websocket_event_data_t*>(event_data));
}

void WsTransport::on_event(std::int32_t id, esp_websocket_event_data_t* data) noexcept {
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            (void)pipe_.post_status(FeedMessage::Kind::Connected);
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
        case WEBSOCKET_EVENT_ERROR: {
            // These three are reported to the feed task and drive NOTHING else,
            // and that is a correction rather than an omission.
            //
            // The retry used to be armed here, on ERROR and CLOSED, on the
            // reasoning that only a definitively-ended attempt may schedule the
            // next one — sound in itself, and it never fired. Two bench outages
            // on 2026-08-09 recovered on the 12 s backstop instead, which is
            // proof the flag was never set, because the armed path returns early
            // and the backstop could not have run if it had been. What this
            // vintage actually dispatches for a dead socket is DISCONNECTED, from
            // abort_connection(); the `Error receive data` line in the log is the
            // library's own ESP_LOGE on the way there, not an ERROR event.
            //
            // Arming on DISCONNECTED instead was the obvious repair and is the
            // wrong one: abort_connection() was also what the supervisor's own
            // stop() ran through, so each restart would re-arm the timer that
            // caused it. (There is no stop() any more — see supervise() — but
            // the rule survives its example: the supervisor polls the socket
            // state directly, which needs no theory about which of these three
            // the library will choose.)
            //
            // Whatever half-written message we held is gone with the socket;
            // drop it before telling the feed task, so the next connection
            // starts from a clean slot.
            // The last socket errno on this task — NOT necessarily the failing
            // call's. This handler runs on the client's own task and newlib
            // errno is per-task, so the value is from the right task; but the
            // library's abort path closes the transport (close_notify write +
            // close(fd)) BEFORE dispatching this event, and either can
            // overwrite errno — usually with a correlated value, not always.
            // Read it as evidence, not verdict: 104=ECONNRESET (something
            // reset the flow), 116=ETIMEDOUT (retransmission gave up),
            // 119=EINPROGRESS-stale (the read set no error at all — the
            // 2026-08-13 signature). For CLOSED events it is whatever the
            // task last set. Calibrated against link_autopsy ground truth
            // before trusting a novel value.
            const int sock_errno = errno;
            reassembler_.reset();
            (void)pipe_.post_status(FeedMessage::Kind::Disconnected);
            // Here to settle which event id this library really raises, since
            // the answer cost an outage to find out and is not written down
            // anywhere in the shipped headers.
            //
            // It is NOT a free line, and the bench output says so: it prints as
            // `[101102][W][ws_transport.cpp:224] on_event(): ...`, Arduino's
            // log_w format, not ESP-IDF's `W (101102) ws:`. That is because this
            // header includes <Arduino.h> first for the INADDR_NONE fix, so
            // esp32-hal-log.h redefines ESP_LOGW to log_w — the log_printfv path
            // that mallocs past 64 chars (this line is ~67) and takes the UART
            // mutex with portMAX_DELAY, which feed_task.cpp had all its logging
            // removed for. Accepted here and not there: this runs on the client
            // task, once, on a socket that has already stopped carrying data, so
            // there is no frame for it to delay. Anything that ever logs from
            // this handler on a LIVE socket has to re-open that argument.
            ESP_LOGW(kTag, "ws down: event %d errno=%d (%s)", static_cast<int>(id),
                     sock_errno, strerror(sock_errno));
            break;
        }

        case WEBSOCKET_EVENT_DATA: {
            if (data == nullptr) { break; }
            // The one place the two handles have to be told apart, and the
            // asymmetry with the status events above is deliberate.
            //
            // A status event from a retired handle is still a true statement —
            // a socket of ours went down — and dropping it would cost a
            // `sock_gaps` count and, if it were the drop that started the
            // outage, the immediate grey that invariant #5 wants. So those are
            // taken from either handle. DATA is different: a chunk fed to the
            // reassembler from a handle we are retiring would interleave with
            // the live one's message and produce a frame that never existed on
            // any wire. It cannot happen today — a retired handle's transport is
            // closed at the abort, so it has nothing left to deliver — and this
            // is the guard for the day that stops being true.
            if (data->client != clients_[live_.load(std::memory_order_relaxed)]) { break; }
            WsChunk chunk;
            chunk.op_code = data->op_code;
            chunk.payload_offset =
                (data->payload_offset > 0) ? static_cast<std::uint32_t>(data->payload_offset) : 0u;
            chunk.payload_len =
                (data->payload_len > 0) ? static_cast<std::uint32_t>(data->payload_len) : 0u;
            chunk.data = data->data_ptr;
            chunk.data_len = (data->data_len > 0) ? static_cast<std::uint32_t>(data->data_len) : 0u;
            // The arrival stamp, taken as far upstream as this firmware can
            // reach: inside the client's own callback, before anything of ours
            // has had a chance to be late. Everything after this point is on our
            // side of the line, which is what makes the arrival-vs-event split a
            // real split rather than two views of the same delay.
            chunk.arrival_us = esp_timer_get_time();
            reassembler_.on_chunk(chunk);
            break;
        }

        default:
            break;
    }
}
#endif  // !DC_OWNED_WS

#if DC_OWNED_WS

bool WsTransport::start() noexcept {
    if (rx_task_ != nullptr) { return true; }

    // Pinned to Core 0, which is the whole of the difference from the old
    // client's task and could not be asked of it: this vintage's
    // esp_websocket_client_config_t has no task_core_id, so its task ran
    // wherever the scheduler put it — sometimes on Core 1 beside the panel's
    // render task, sometimes not, and never the same across two bench runs.
    const BaseType_t ok =
        xTaskCreatePinnedToCore(&WsTransport::rx_trampoline, "dc_ws", kRxTaskStack, this,
                                kRxTaskPriority, &rx_task_, kRxTaskCore);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "could not create the RX task");
        rx_task_ = nullptr;
        return false;
    }

    // The boot connection is attempt #1, and marking it is not bookkeeping — it
    // is what buys it the same handshake immunity every retry gets. Without this
    // the supervisor sees a client that has simply never been connected, cannot
    // tell that from one that dropped, and asks for another socket a backoff in:
    // straight through the cold TLS handshake, which the 2026-08-09 18:15 log
    // caught the old client doing.
    supervisor_.note_attempt_begun(esp_timer_get_time());
    connect_requested_.store(true, std::memory_order_relaxed);
    ESP_LOGI(kTag, "connecting to %s — owned client, RX task on core %d prio %d", kAnvilUri,
             static_cast<int>(kRxTaskCore), static_cast<int>(kRxTaskPriority));
    return true;
}

void WsTransport::rx_trampoline(void* arg) noexcept { static_cast<WsTransport*>(arg)->rx_main(); }

void WsTransport::rx_main() noexcept {
    for (;;) {
        if (tls_ == nullptr) {
            // Nothing to read and nothing to decide: the supervisor owns WHEN,
            // exactly as it did when this was esp_websocket_client_start() on
            // loopTask. 20 ms is a poll of a flag, an order of magnitude finer
            // than the 250 ms cadence that sets it, so it adds nothing
            // measurable to a recovery.
            if (connect_requested_.exchange(false, std::memory_order_relaxed)) {
                (void)open_socket();
            } else {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        }

        const int rc = static_cast<int>(esp_tls_conn_read(tls_, rx_buf_, sizeof(rx_buf_)));
        // Read before anything else can touch it. newlib's errno is per-task and
        // this is the failing call's own task, so this is the strongest version
        // of the datum the old client could only approximate — its handler ran
        // after abort_connection() had already closed the transport, and either
        // the close_notify write or the close(fd) could overwrite the value.
        const int read_errno = errno;
        // THE ARRIVAL STAMP, and it is now as far upstream as this firmware can
        // possibly reach: the instruction after the read that produced the
        // bytes. It used to be taken inside esp_websocket_client's callback,
        // which is already downstream of an esp_event dispatch hop — so the
        // arrival-vs-event split (strain 12) got sharper for free, and a hole on
        // the arrival side can no longer be that library's scheduling.
        const std::int64_t at_us = esp_timer_get_time();

        if (rc > 0) {
            socket_bytes_ += static_cast<std::uint64_t>(rc);
            parser_.feed(rx_buf_, static_cast<std::uint32_t>(rc), at_us);
            if (parser_.failed()) {
                // The stream is no longer a WebSocket stream. A desynced parser
                // cannot resynchronise by reading further, so the socket goes.
                die(ws_error_name(parser_.error()), 0, 0);
                continue;
            }
            if (close_code_ >= 0) {   // the server said goodbye in-band
                die("ws-close-frame", 0, 0);
                continue;
            }
            if (!maybe_ping(at_us)) { continue; }
        } else if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // SO_RCVTIMEO expired. NOT a death: Anvil's worst healthy gap is
            // ~600 ms and the 2026-08-13 fade record has measured silences to
            // 3.9 s on a weak association that never dropped a socket. What
            // greys the panel for silence is the feed task's 1 s RX watchdog,
            // which is a statement about DATA and belongs there.
            if (!maybe_ping(at_us)) { continue; }
        } else {
            // Unless the floor moved: the association can fall inside the
            // blocking read, in which case lwIP aborts the pcb and the error
            // belongs to the radio rather than to the flow. It is still
            // autopsied — the label `assoc=0` on the line is what separates
            // them — because a death that is not counted is a death that cannot
            // be compared against the old client's.
            die(rc == 0 ? "clean-close" : "read", rc, read_errno);
        }
    }
}

bool WsTransport::open_socket() noexcept {
    // Everything the last socket left behind, cleared before the next one can
    // put a byte anywhere. The reassembler's reset is what makes a half-written
    // message die with its connection instead of being completed by the next.
    parser_.reset();
    reassembler_.reset();
    close_code_ = -1;
    close_reason_[0] = '\0';
    socket_bytes_ = 0;
    fd_ = -1;

    const std::int64_t dns_us = warm_dns();
    const std::int64_t t0 = esp_timer_get_time();
    opened_us_ = t0;
    last_ping_us_ = t0;

    esp_tls_cfg_t cfg = {};
    // The production trust anchor, unchanged and still pinned: ISRG Root X1/X2
    // in anvil_root_ca.hpp, with the full chain and the reasoning there. The
    // certificate bundle remains unreachable in this framework — that was never
    // the websocket client's doing, so removing it changes nothing here.
    cfg.cacert_buf = reinterpret_cast<const unsigned char*>(kAnvilRootCaPem);
    cfg.cacert_bytes = sizeof(kAnvilRootCaPem);   // PEM: the NUL is part of it
    cfg.timeout_ms = static_cast<int>(kConnectTimeoutMs);

    tls_ = esp_tls_init();
    if (tls_ == nullptr) {
        autopsy("tls-init", -1, ENOMEM);
        return false;
    }
    if (esp_tls_conn_new_sync(kAnvilHost, static_cast<int>(std::strlen(kAnvilHost)), kAnvilPort,
                              &cfg, tls_) != 1) {
        die("connect", -1, errno);
        return false;
    }
    (void)esp_tls_get_conn_sockfd(tls_, &fd_);

    // A read that blocks forever cannot notice anything; a write that blocks
    // forever holds the feed core. Both are bounded here rather than by hope.
    timeval rcv{};
    rcv.tv_sec = kReadTimeoutMs / 1000;
    rcv.tv_usec = (kReadTimeoutMs % 1000) * 1000;
    (void)setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
    timeval snd{};
    snd.tv_sec = 5;
    (void)setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));

    if (!http_upgrade()) {
        close_socket();
        return false;
    }

    socket_up_.store(true, std::memory_order_relaxed);
    // A request that arrived WHILE this connect was in flight is spent. The
    // supervisor's budget is 7 s and a cold connect has measured 4.2 s, so a
    // slow one can be asked for a second socket before the first has answered;
    // without this the stale flag would survive to the next death and reconnect
    // with no backoff and no association gate.
    connect_requested_.store(false, std::memory_order_relaxed);
    (void)pipe_.post_status(FeedMessage::Kind::Connected);
    ESP_LOGI(kTag, "socket up: dns %d ms, connect+upgrade %d ms, fd %d, rssi %d dBm",
             static_cast<int>(dns_us / 1000),
             static_cast<int>((esp_timer_get_time() - t0) / 1000), fd_,
             static_cast<int>(WiFi.RSSI()));
    return true;
}

bool WsTransport::http_upgrade() noexcept {
    char req[320];
    const int n = std::snprintf(req, sizeof(req),
                                "GET %s HTTP/1.1\r\n"
                                "Host: %s\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Key: %s\r\n"
                                "Sec-WebSocket-Version: 13\r\n"
                                "\r\n",
                                kAnvilPath, kAnvilHost, kWsKey);
    // No Origin header. ARCHITECTURE §7 closed this: M0 measured the deployed
    // upgrade accepting a client that sends none. If that ever changes the
    // server refuses the upgrade loudly, right here, and the one-line fallback
    // is an `Origin: https://anvil.garethcooke.com\r\n` field above.
    if (n <= 0 || n >= static_cast<int>(sizeof(req))) {
        ESP_LOGE(kTag, "upgrade request would not fit — check kAnvilPath");
        return false;
    }
    if (!write_all(req, static_cast<std::size_t>(n))) {
        autopsy("upgrade-write", -1, errno);
        return false;
    }

    // ONE BYTE AT A TIME, and it is not laziness. The header block must be
    // consumed to exactly the blank line and not one byte further, because
    // anything the server sent after it is already a WebSocket frame — reading
    // ahead into a buffer and then discarding the remainder is how a client
    // manufactures the stray leading byte this whole rebuild exists to remove.
    // It costs ~150 single-byte TLS reads, once per connection.
    char hdr[512];
    std::size_t at = 0;
    const std::int64_t deadline = esp_timer_get_time() + kUpgradeBudgetUs;
    bool complete = false;
    while (at + 1 < sizeof(hdr)) {
        const int r = static_cast<int>(esp_tls_conn_read(tls_, hdr + at, 1));
        if (r == 1) {
            ++at;
            if (at >= 4 && std::memcmp(hdr + at - 4, "\r\n\r\n", 4) == 0) {
                complete = true;
                break;
            }
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (esp_timer_get_time() > deadline) {
                autopsy("upgrade-timeout", 0, errno);
                return false;
            }
            continue;
        }
        autopsy(r == 0 ? "upgrade-closed" : "upgrade-read", r, errno);
        return false;
    }
    hdr[at] = '\0';
    if (!complete) {
        ESP_LOGE(kTag, "upgrade headers did not end in %u bytes", static_cast<unsigned>(sizeof(hdr)));
        return false;
    }

    if (std::strstr(hdr, " 101 ") == nullptr) {
        // 120 characters is the status line and the first field or two, which is
        // where a refusal says why.
        ESP_LOGE(kTag, "upgrade refused: %.120s", hdr);
        return false;
    }
    // The check the prototype skipped. It cannot fail on a healthy Anvil, which
    // is the point: if it ever does, something answered the upgrade without
    // doing the SHA1, and finding that out here costs one string compare instead
    // of a socket's worth of frame-parser confusion.
    if (!header_begins_with(hdr, "sec-websocket-accept:", kWsAccept)) {
        ESP_LOGE(kTag, "upgrade accept mismatch — expected %s; headers: %.160s", kWsAccept, hdr);
        return false;
    }
    return true;
}

void WsTransport::close_socket() noexcept {
    // Ours, and it returns at once. This single call is what deleted the spare
    // handle, the 5 s sleeper, the auto-reconnect flag and the retired-handle
    // guard: `esp_websocket_client_stop()` blocked for up to 5 s on a task that
    // could not observe its own stop flag until it woke, and every design
    // decision in ARCHITECTURE §9's 2026-08-10 entry existed to route around it.
    if (tls_ != nullptr) {
        esp_tls_conn_destroy(tls_);
        tls_ = nullptr;
    }
    fd_ = -1;
    // Here and not at the next open: the reassembler is holding a FramePipe slot
    // for whatever half-written message the socket took with it, and a slot that
    // comes back only when the next connection succeeds is a slot lost for the
    // whole of an outage.
    reassembler_.reset();
    parser_.reset();

    // Only a socket that was UP owes the feed task a Disconnected. A connect
    // that never completed leaves the book already stale from the drop that
    // preceded it, and posting a second Gap for it would put a disconnect in the
    // histogram that no connection ever matched.
    if (socket_up_.exchange(false, std::memory_order_relaxed)) {
        (void)pipe_.post_status(FeedMessage::Kind::Disconnected);
    }
}

bool WsTransport::write_all(const void* data, std::size_t len) noexcept {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    std::size_t sent = 0;
    const std::int64_t deadline = esp_timer_get_time() + kWriteBudgetUs;
    while (sent < len) {
        const int rc = static_cast<int>(esp_tls_conn_write(tls_, p + sent, len - sent));
        if (rc > 0) {
            sent += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (esp_timer_get_time() > deadline) { return false; }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        return false;
    }
    return true;
}

bool WsTransport::maybe_ping(std::int64_t now) noexcept {
    if (kClientPingMs == 0) { return true; }
    if (now - last_ping_us_ < static_cast<std::int64_t>(kClientPingMs) * 1000) { return true; }
    last_ping_us_ = now;
    // FIN + ping, masked with an all-zero key: a client MUST mask (§5.3) and an
    // all-zero key is a legal one that leaves the (empty) payload unchanged.
    const std::uint8_t ping[6] = {0x89, 0x80, 0, 0, 0, 0};
    if (!write_all(ping, sizeof(ping))) {
        die("ping-write", -1, errno);
        return false;
    }
    return true;
}

// Autopsy FIRST, teardown second, and the order is the whole reason this
// function exists: autopsy() reads fd_, tls_->error_handle, the parser's frame
// counts and close_code_, and close_socket() destroys or zeroes every one of
// them. Five call sites each kept that ordering by convention; a sixth that
// flipped it would compile and print an autopsy of zeros — a silently degraded
// instrument, which is the one failure this file is not allowed to have.
void WsTransport::die(const char* what, int rc, int saved_errno) noexcept {
    autopsy(what, rc, saved_errno);
    close_socket();
}

void WsTransport::on_ping(const std::uint8_t* payload, std::uint32_t len) noexcept {
    pipe_.count_control();
    // The pong is written from inside the parser's callback, which is safe for
    // one reason worth stating: this is the RX task, and the read that produced
    // these bytes has already returned — there is no mbedtls call on the stack
    // beneath us. Doing it here rather than after feed() also gets the case of
    // two pings in one read right, which a single "pong owed" flag would not.
    std::uint8_t pong[2 + 4 + kMaxControlPayload];
    pong[0] = 0x8A;                                              // FIN + pong
    pong[1] = static_cast<std::uint8_t>(0x80u | (len & 0x7Fu));   // masked, len <= 125
    std::memset(pong + 2, 0, 4);
    if (len > 0) { std::memcpy(pong + 6, payload, len); }
    if (!write_all(pong, 6u + len)) {
        // A short or failed pong desyncs nothing here (we send whole frames or
        // fail), but a server that enforces pongs will close, and this line is
        // what stops that reading as a server-side kill. Logged on a live socket
        // — the one place this firmware does — because it is rare, it means the
        // socket is already dying, and without it the next autopsy has no cause.
        ESP_LOGW(kTag, "pong write failed (errno %d) — the socket is going", errno);
    }
}

void WsTransport::on_close(std::uint16_t code, const char* reason,
                           std::uint32_t reason_len) noexcept {
    // NOT counted as a control frame, and the asymmetry with on_ping/on_pong is
    // deliberate. FrameReassembler has always treated a close as a no-op rather
    // than a control count ("a close frame is ignored here; the disconnect path
    // does the work"), so counting it here would make `control` mean a different
    // thing in the two build arms — and the only reason the espws arm still
    // exists is to have its counters compared against this one.
    close_code_ = static_cast<int>(code);
    const std::size_t room = sizeof(close_reason_) - 1;
    const std::size_t n = (reason_len < room) ? reason_len : room;
    if (n > 0) { std::memcpy(close_reason_, reason, n); }
    close_reason_[n] = '\0';
    // The socket is not closed here — rx_main() does it, after feed() returns
    // and the buffer it is walking is no longer in use.
}

void WsTransport::autopsy(const char* what, int rc, int saved_errno) noexcept {
    const std::int64_t now = esp_timer_get_time();
    const std::int64_t lifetime_ms = (opened_us_ > 0) ? (now - opened_us_) / 1000 : 0;

    int so_error = 0;
    socklen_t so_len = sizeof(so_error);
    if (fd_ >= 0) { (void)getsockopt(fd_, SOL_SOCKET, SO_ERROR, &so_error, &so_len); }

    int tls_code = 0;
    int tls_flags = 0;
    if (tls_ != nullptr && tls_->error_handle != nullptr) {
        (void)esp_tls_get_and_clear_last_error(tls_->error_handle, &tls_code, &tls_flags);
    }

    char mbed[96] = "n/a";
    if (rc < 0) { mbedtls_strerror(rc, mbed, sizeof(mbed)); }

    ++deaths_;
    ESP_LOGW(kTag, "socket end #%u [%s]: %d ms, %llu bytes, %u data / %u ctrl frames",
             static_cast<unsigned>(deaths_), what, static_cast<int>(lifetime_ms),
             static_cast<unsigned long long>(socket_bytes_),
             static_cast<unsigned>(parser_.data_frames()),
             static_cast<unsigned>(parser_.control_frames()));
    ESP_LOGW(kTag, "  rc=%s0x%04X (%s) errno=%d (%s) so_error=%d esp_tls=0x%X/0x%X",
             (rc < 0) ? "-" : "", static_cast<unsigned>((rc < 0) ? -rc : rc), mbed, saved_errno,
             std::strerror(saved_errno), so_error, static_cast<unsigned>(tls_code),
             static_cast<unsigned>(tls_flags));
    // BYTES, not words: ESP-IDF's port defines StackType_t as uint8_t, so the
    // high-water mark comes back in bytes (feed_task.hpp quotes the same trap
    // for stack SIZES). The first version printed "words", which overstated the
    // margin 4x on the line whose stated job is to lower kRxTaskStack safely.
    ESP_LOGW(kTag, "  rssi=%d dBm assoc=%d stack_free=%u B",
             static_cast<int>(WiFi.RSSI()), (WiFi.status() == WL_CONNECTED) ? 1 : 0,
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    if (close_code_ >= 0) {
        ESP_LOGW(kTag, "  ws close frame: code %d reason '%s' <- the server SAID why", close_code_,
                 close_reason_);
    }
}

#endif  // DC_OWNED_WS

}  // namespace depthcharge::fw
