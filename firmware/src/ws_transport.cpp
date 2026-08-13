// firmware/src/ws_transport.cpp — see ws_transport.hpp.
#include "ws_transport.hpp"

#include <WiFi.h>

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"

#include "anvil_root_ca.hpp"

namespace depthcharge::fw {
namespace {
constexpr const char* kTag = "ws";
// A/B in the log, because "handle 1" and "attempt #1" beside each other at a
// bench at 11pm is one transposition away from a wrong diagnosis.
constexpr char kHandleName[] = {'A', 'B'};
}  // namespace

bool WsTransport::connect_wifi(const char* ssid, const char* password,
                               std::uint32_t timeout_ms) noexcept {
    // Kept so supervise() can rejoin later. This function used to be the ONLY
    // place the station was ever asked to associate, which is the hole the
    // 2026-08-10 bench fell into — see WifiSupervisor.
    ssid_ = ssid;
    password_ = password;

    WiFi.mode(WIFI_STA);
    // Scan every channel and join by SIGNAL, not by whoever answers first.
    // The framework default is WIFI_FAST_SCAN: first probe response wins. On a
    // mesh where every node broadcasts the same SSID that is a lottery, and the
    // 2026-08-13 bench measured it drawing a −76 dBm sibling in half its boots
    // while a −40 dBm node stood beside it (bssid history in
    // hardware/bench-2026-08-13-wifi-drop-diagnosis.md). The board never roams
    // off its draw, so the boot-time choice is the association for the run.
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
    WiFi.begin(ssid, password);

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

void WsTransport::supervise() noexcept {
    if (clients_[0] == nullptr) { return; }

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
            ESP_LOGI(kTag, "socket up on handle %c, %d ms into attempt #%u",
                     kHandleName[live_.load(std::memory_order_relaxed)],
                     static_cast<int>(d.elapsed_us / 1000),
                     static_cast<unsigned>(d.attempt));
            break;

        case SupervisorAction::StartAttempt:
            (void)open_spare(d);
            break;

        case SupervisorAction::None:
            break;
    }
}

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

}  // namespace depthcharge::fw
