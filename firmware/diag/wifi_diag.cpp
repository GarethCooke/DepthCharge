// firmware/diag/wifi_diag.cpp — the link, and nothing else.
//
// A standalone diagnostic sketch for one question: WHY does the association
// keep dying? Built after the 2026-08-13 bench run showed a reconnect every
// ~7 s at rssi −78…−80 dBm — the same neighbourhood as the far-node
// mis-association the M3 stage D brief records as solved at −34 dBm — and
// after a bare DevKit and a second board reproduced it, which acquits the
// panel wiring and the silicon and leaves the link itself as the suspect.
//
// It answers with instruments the main firmware deliberately does not carry:
//
//   1. A per-BSSID survey of the mesh SSID. Every Deco node broadcasts the
//      same SSID, so `rssi=-78` in the main log cannot say WHICH node the
//      board is attached to, or whether a −35 dBm node was sitting there
//      ignored. The scan prints every matching BSSID with channel and rssi,
//      flags the one we are on, and calls out a mis-association explicitly.
//   2. WIFI_EVENT_STA_DISCONNECTED reason codes — the instrument M3 parked.
//      BEACON_TIMEOUT (200) is the radio losing the AP: an RF/coverage
//      problem. AUTH_EXPIRE / ASSOC_EXPIRE / ASSOC_LEAVE is the AP ending it:
//      a mesh/steering problem. The two need opposite fixes, and the main
//      firmware currently cannot tell them apart.
//   3. A DNS + plain-TCP probe against the Anvil authority every 10 s, timed
//      separately, so "the Wi-Fi is up but the path is dying" is measured
//      rather than inferred from websocket wreckage.
//   4. A BSSID pin ('p' over serial): reconnect locked to the strongest node
//      the last scan saw. If drops stop under the pin, the diagnosis is
//      mis-association and the fix is a pinned (or steered) association; if
//      they continue at −35 dBm, the problem is in front of the antenna, not
//      the roaming decision.
//
// No engine/, no panel, no websocket, no TLS. Association parameters mirror
// the main firmware exactly where it matters: STA mode, modem sleep OFF
// before begin() (same call order — see ws_transport.cpp for why the order
// makes it stick) — with ONE deliberate exception: scan method stays at the
// framework's FAST_SCAN default, because observing that lottery is this
// sketch's job (the production firmware joins by signal as of 2026-08-13). Everything prints through Serial directly with a millis()
// stamp; the monitor's `time` filter adds wall clock on the host side.
//
// Build and run (owner-driven, from firmware/):
//     pio run -e wifi-diag -t upload
//     pio device monitor -e wifi-diag
// Serial commands: s = scan now · p = pin to strongest · u = unpin · ? = help
#include <Arduino.h>
#include <WiFi.h>

#include <cstdint>
#include <cstring>

#include "esp_wifi.h"
#include "lwip/netdb.h"

#include "secrets.h"

namespace {

// The Anvil authority, spelled the same as ws_transport.hpp spells it. Plain
// TCP to 443 — the handshake we never start is the point: this times the path,
// not the TLS stack.
constexpr char kProbeHost[]     = "anvil.garethcooke.com";
constexpr uint16_t kProbePort   = 443;

constexpr uint32_t kStatusPeriodMs  = 1000;   // rssi/bssid line
constexpr uint32_t kProbePeriodMs   = 10000;  // dns + tcp probe
constexpr uint32_t kScanPeriodMs    = 60000;  // async re-scan while connected
constexpr uint32_t kSummaryPeriodMs = 30000;  // reason histogram + assoc stats
constexpr uint32_t kRejoinAfterMs   = 10000;  // manual begin() if down this long

// --- association bookkeeping, written from the Wi-Fi event task --------------
// Single writer (the event task), read from loop() for printing. The same
// unsynchronised-diagnostics trade every counter in the main firmware makes.
volatile uint32_t g_disconnects     = 0;
volatile uint32_t g_assoc_start_ms  = 0;
volatile bool     g_associated      = false;
uint32_t g_assoc_longest_ms  = 0;
uint32_t g_assoc_shortest_ms = 0xFFFFFFFF;

// reason -> count, linear probe. 16 distinct reasons is far more than one
// bench run produces; overflow lands in the last slot as "other".
struct ReasonCount { uint8_t reason; uint32_t count; };
ReasonCount g_reasons[16] = {};

// --- last scan's view of the mesh, written from loop() only ------------------
uint8_t g_best_bssid[6] = {};
int32_t g_best_channel  = 0;
int32_t g_best_rssi     = -127;
bool    g_best_valid    = false;
bool    g_pinned        = false;

const char* reason_name(uint8_t r) {
    switch (r) {
        case 1:   return "UNSPECIFIED";
        case 2:   return "AUTH_EXPIRE";           // AP: authentication lapsed
        case 3:   return "AUTH_LEAVE";
        case 4:   return "ASSOC_EXPIRE";          // AP: association lapsed
        case 5:   return "ASSOC_TOOMANY";
        case 8:   return "ASSOC_LEAVE";           // we (or the driver) left
        case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
        case 200: return "BEACON_TIMEOUT";        // radio lost the AP: RF
        case 201: return "NO_AP_FOUND";
        case 202: return "AUTH_FAIL";
        case 203: return "ASSOC_FAIL";
        case 204: return "HANDSHAKE_TIMEOUT";
        case 205: return "CONNECTION_FAIL";
        case 206: return "AP_TSF_RESET";
        case 207: return "ROAMING";               // driver moving between nodes
        default:  return "?";
    }
}

void count_reason(uint8_t r) {
    for (auto& slot : g_reasons) {
        if (slot.count == 0 || slot.reason == r) {
            slot.reason = r;
            ++slot.count;
            return;
        }
    }
    ++g_reasons[15].count;  // shared "other" bucket once the table is full
}

void on_wifi_event(arduino_event_id_t id, arduino_event_info_t info) {
    const uint32_t now = millis();
    switch (id) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
            const auto& c = info.wifi_sta_connected;
            g_assoc_start_ms = now;
            g_associated = true;
            Serial.printf("[%8lu] EVT associated: bssid %02X:%02X:%02X:%02X:%02X:%02X ch %u\r\n",
                          (unsigned long)now, c.bssid[0], c.bssid[1], c.bssid[2], c.bssid[3],
                          c.bssid[4], c.bssid[5], (unsigned)c.channel);
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[%8lu] EVT got ip: %s\r\n", (unsigned long)now,
                          WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            const uint8_t r = info.wifi_sta_disconnected.reason;
            const uint32_t lived = g_associated ? (now - g_assoc_start_ms) : 0;
            g_associated = false;
            ++g_disconnects;
            count_reason(r);
            if (lived > 0) {
                if (lived > g_assoc_longest_ms)  { g_assoc_longest_ms = lived; }
                if (lived < g_assoc_shortest_ms) { g_assoc_shortest_ms = lived; }
            }
            // THE line this sketch exists for. reason + how long the
            // association lived is the whole diagnosis in one row.
            Serial.printf("[%8lu] EVT DISCONNECT #%lu reason %u (%s) after %lu ms associated\r\n",
                          (unsigned long)now, (unsigned long)g_disconnects, (unsigned)r,
                          reason_name(r), (unsigned long)lived);
            break;
        }
        default:
            break;
    }
}

// Print every visible BSSID broadcasting our SSID, remember the strongest,
// and say out loud whether we are camped on the wrong one. Also one line of
// per-channel AP counts (all SSIDs) — co-channel congestion context for free.
void report_scan(int16_t n) {
    uint8_t ch_load[15] = {};  // 2.4 GHz channels 1..14
    g_best_rssi = -127;
    g_best_valid = false;

    wifi_ap_record_t cur{};
    const bool have_cur = (esp_wifi_sta_get_ap_info(&cur) == ESP_OK);

    Serial.printf("[%8lu] scan: %d networks visible\r\n", (unsigned long)millis(), (int)n);
    for (int16_t i = 0; i < n; ++i) {
        const int32_t ch = WiFi.channel(i);
        if (ch >= 1 && ch <= 14) { ++ch_load[ch]; }
        if (WiFi.SSID(i) != kWifiSsid) { continue; }

        const uint8_t* b = WiFi.BSSID(i);
        const int32_t rssi = WiFi.RSSI(i);
        const bool is_current = have_cur && (memcmp(b, cur.bssid, 6) == 0);
        if (rssi > g_best_rssi) {
            memcpy(g_best_bssid, b, 6);
            g_best_channel = ch;
            g_best_rssi = rssi;
            g_best_valid = true;
        }
        Serial.printf("[%8lu]   %s %02X:%02X:%02X:%02X:%02X:%02X ch %2d  %4d dBm%s\r\n",
                      (unsigned long)millis(), kWifiSsid, b[0], b[1], b[2], b[3], b[4], b[5],
                      (int)ch, (int)rssi, is_current ? "  <-- associated here" : "");
    }
    Serial.printf("[%8lu]   ch load:", (unsigned long)millis());
    for (int ch = 1; ch <= 14; ++ch) {
        if (ch_load[ch] > 0) { Serial.printf(" %d:%u", ch, (unsigned)ch_load[ch]); }
    }
    Serial.print("\r\n");

    // The verdict the main firmware cannot give: a stronger node was visible
    // while we sat on a weak one. 10 dB is well past scan-to-scan jitter.
    if (have_cur && g_best_valid && memcmp(g_best_bssid, cur.bssid, 6) != 0 &&
        g_best_rssi >= cur.rssi + 10) {
        Serial.printf("[%8lu]   *** MIS-ASSOCIATED: on %d dBm node while a %d dBm node is visible"
                      " — try 'p' to pin ***\r\n",
                      (unsigned long)millis(), (int)cur.rssi, (int)g_best_rssi);
    }
    if (!g_best_valid) {
        Serial.printf("[%8lu]   *** SSID '%s' NOT VISIBLE on 2.4 GHz at all ***\r\n",
                      (unsigned long)millis(), kWifiSsid);
    }
}

// DNS and TCP timed apart, exactly the split warm_dns() makes in the main
// firmware, so the numbers line up across logs.
void tcp_probe() {
    if (WiFi.status() != WL_CONNECTED) { return; }
    const uint32_t t0 = millis();
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const int rc = ::getaddrinfo(kProbeHost, "443", &hints, &res);
    const uint32_t t1 = millis();
    if (rc != 0 || res == nullptr) {
        Serial.printf("[%8lu] probe: DNS FAIL rc %d after %lu ms\r\n",
                      (unsigned long)t1, rc, (unsigned long)(t1 - t0));
        return;
    }
    const IPAddress ip(reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr);
    ::freeaddrinfo(res);

    WiFiClient client;
    const bool ok = client.connect(ip, kProbePort, 4000);
    const uint32_t t2 = millis();
    client.stop();
    Serial.printf("[%8lu] probe: dns %lu ms, tcp %s %lu ms (%s)\r\n",
                  (unsigned long)t2, (unsigned long)(t1 - t0), ok ? "up" : "FAIL",
                  (unsigned long)(t2 - t1), ip.toString().c_str());
}

void print_summary() {
    const uint32_t now = millis();
    Serial.printf("[%8lu] -- summary: drops=%lu assoc longest=%lu ms shortest=%lu ms pinned=%s\r\n",
                  (unsigned long)now, (unsigned long)g_disconnects,
                  (unsigned long)g_assoc_longest_ms,
                  (g_assoc_shortest_ms == 0xFFFFFFFF) ? 0ul : (unsigned long)g_assoc_shortest_ms,
                  g_pinned ? "yes" : "no");
    Serial.printf("[%8lu] -- reasons:", (unsigned long)now);
    for (const auto& slot : g_reasons) {
        if (slot.count > 0) {
            Serial.printf(" %u(%s)x%lu", (unsigned)slot.reason, reason_name(slot.reason),
                          (unsigned long)slot.count);
        }
    }
    Serial.print("\r\n");
}

void begin_wifi(bool pinned) {
    g_pinned = pinned && g_best_valid;
    WiFi.disconnect();  // clears WL_CONNECT_FAILED, same as the supervisor
    if (g_pinned) {
        Serial.printf("[%8lu] connecting PINNED to %02X:%02X:%02X:%02X:%02X:%02X ch %d (%d dBm)\r\n",
                      (unsigned long)millis(), g_best_bssid[0], g_best_bssid[1], g_best_bssid[2],
                      g_best_bssid[3], g_best_bssid[4], g_best_bssid[5], (int)g_best_channel,
                      (int)g_best_rssi);
        WiFi.begin(kWifiSsid, kWifiPassword, g_best_channel, g_best_bssid);
    } else {
        Serial.printf("[%8lu] connecting UNPINNED (driver picks the node)\r\n",
                      (unsigned long)millis());
        WiFi.begin(kWifiSsid, kWifiPassword);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);  // the USB-serial link enumerates after boot

    Serial.print("\r\n[       0] wifi-diag: the link, and nothing else\r\n"
                 "[       0] commands: s=scan  p=pin-to-strongest  u=unpin  ?=help\r\n");

    WiFi.onEvent(on_wifi_event);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);   // mirror the main firmware: modem sleep OFF, set
                            // after mode() and before begin() so it sticks
    WiFi.disconnect();
    delay(100);

    // Survey BEFORE the first association — the only moment a scan is free —
    // so the run opens with the full map of the mesh as this antenna sees it.
    const int16_t n = WiFi.scanNetworks(/*async=*/false);
    report_scan(n);
    WiFi.scanDelete();

    begin_wifi(/*pinned=*/false);
}

void loop() {
    static uint32_t last_status = 0, last_probe = 0, last_summary = 0;
    static uint32_t last_scan = 0, down_since = 0;
    static bool scanning = false;
    const uint32_t now = millis();

    // Serial commands.
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c == 's' && !scanning) {
            scanning = (WiFi.scanNetworks(/*async=*/true) == WIFI_SCAN_RUNNING);
        } else if (c == 'p') {
            if (g_best_valid) { begin_wifi(true); }
            else { Serial.print("no scan result to pin to — 's' first\r\n"); }
        } else if (c == 'u') {
            begin_wifi(false);
        } else if (c == '?') {
            Serial.print("s=scan  p=pin-to-strongest  u=unpin  ?=help\r\n");
        }
    }

    // Async scan reaper.
    if (scanning) {
        const int16_t n = WiFi.scanComplete();
        if (n >= 0) {
            report_scan(n);
            WiFi.scanDelete();
            scanning = false;
        } else if (n == WIFI_SCAN_FAILED) {
            Serial.printf("[%8lu] scan failed\r\n", (unsigned long)now);
            scanning = false;
        }
    } else if (now - last_scan >= kScanPeriodMs) {
        // Periodic survey while connected. The radio goes off-channel between
        // beacons for it — a few tens of ms of extra latency per channel, which
        // is acceptable in a diagnostic and is itself a data point: if drops
        // cluster on scans, the link has no margin at all.
        last_scan = now;
        scanning = (WiFi.scanNetworks(/*async=*/true) == WIFI_SCAN_RUNNING);
    }

    // One rssi/bssid line a second while associated.
    if (now - last_status >= kStatusPeriodMs) {
        last_status = now;
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            down_since = 0;
            Serial.printf("[%8lu] rssi %4d dBm  bssid %02X:%02X:%02X:%02X:%02X:%02X ch %2u"
                          "  assoc %lu s  drops %lu%s\r\n",
                          (unsigned long)now, (int)ap.rssi, ap.bssid[0], ap.bssid[1], ap.bssid[2],
                          ap.bssid[3], ap.bssid[4], ap.bssid[5], (unsigned)ap.primary,
                          (unsigned long)((now - g_assoc_start_ms) / 1000),
                          (unsigned long)g_disconnects, g_pinned ? "  [pinned]" : "");
        } else {
            if (down_since == 0) { down_since = now; }
            Serial.printf("[%8lu] not associated (%lu ms)\r\n", (unsigned long)now,
                          (unsigned long)(now - down_since));
            // Arduino's auto-reconnect gives up permanently on AUTH_FAIL (the
            // main firmware's WifiSupervisor exists for exactly this); a dumb
            // timed rejoin is enough for a diagnostic.
            if (now - down_since >= kRejoinAfterMs) {
                down_since = 0;
                begin_wifi(g_pinned);
            }
        }
    }

    if (now - last_probe >= kProbePeriodMs)     { last_probe = now; tcp_probe(); }
    if (now - last_summary >= kSummaryPeriodMs) { last_summary = now; print_summary(); }

    delay(50);
}
