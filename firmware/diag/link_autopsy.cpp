// firmware/diag/link_autopsy.cpp — how, exactly, does the socket die?
//
// The 2026-08-13 storm log (device-monitor-260813-095512.log) establishes four
// things: the Wi-Fi association NEVER dropped (74 socket deaths, zero STA
// disconnect reasons), every TLS handshake succeeded at normal speed, each
// socket lived a near-constant ~2 s / ~100 KB after coming up, and the storm
// started and stopped on its own clock with no board change. Something is
// ending established flows — and esp_websocket_client reports every variant of
// that as the same two lines ('Error read data' / 'Error receive data'),
// swallowing the one number that names the killer. This vintage exposes no
// error_handle on the client config, so the datum is unreachable from the main
// firmware: the only way to get it is to own the socket.
//
// This sketch owns the socket. It speaks just enough TLS (esp-tls) and just
// enough WebSocket (a hand-rolled upgrade + frame parser) to hold the real
// Anvil feed open and read it flat out, and on every death prints the autopsy
// the library never could:
//
//   rc      the exact mbedtls error from the failing read/write, in hex.
//           -0x0050 CONN_RESET / errno 104 -> an ACTIVE TCP RST arrived: an
//           in-path box (mesh node NAT/steering/DPI, conntrack eviction) or
//           the server killed the flow.
//           -0x7180 INVALID_MAC / -0x7200 INVALID_RECORD -> bytes were
//           CORRUPTED in flight but passed TCP's checksum: failing forwarding
//           hardware in the path (TLS's MAC is the first honest detector).
//           -0x7780 FATAL_ALERT -> the server's TLS stack objected out loud.
//           -0x7F00 ALLOC_FAILED / errno 12 -> our own stack ran dry (the one
//           board-side cause the storm evidence leaves alive).
//           rc=0 -> a CLEAN close: FIN/close_notify, i.e. deliberate policy,
//           not failure.
//   errno / SO_ERROR   the socket layer's own account, captured at the fault.
//   ws close frame     if the server sent one, its status code and reason
//                      text — a server enforcing a policy usually says so.
//   bytes + ms         lifetime and bytes-at-death. ~Constant BYTES across
//                      deaths = a byte-counting middlebox; ~constant SECONDS =
//                      a timer; neither = weather.
//
// Three flows, switched over serial, same autopsy on each — because WHERE a
// kill does and does not reproduce is the split that names the killer:
//
//   1  (default) the real Anvil feed: TLS + WS to anvil.garethcooke.com,
//      pinned root CA, exactly the production path.
//   2  TLS bulk from an UNRELATED host (speed.cloudflare.com, verification
//      off — trust is not what is under test). Dies the same way -> the killer
//      is in-path and flow-shape-keyed, and Anvil/AWS are exonerated.
//   3  plain-TCP bulk (no TLS, port 80). TLS arms die while this survives ->
//      the damage is corruption TLS alone can see; this dying too -> the
//      killer acts at the TCP/flow level.
//
// Wi-Fi mirrors the main firmware (STA, modem sleep off before begin()) with
// ONE deliberate exception: scan method stays at the framework's FAST_SCAN
// default, so the unpinned arm reproduces the boot lottery the production
// firmware carried until 2026-08-13 (it now scans all channels and joins by
// signal). STA disconnect reasons are logged so the 'association held through
// it' finding keeps proving itself in the same capture.
//
// Build and run (owner-driven, from firmware/):
//     pio run -e link-autopsy -t upload
//     pio device monitor -e link-autopsy
// Serial: 1=anvil-ws  2=tls-bulk  3=tcp-bulk  p=pin-sick-node  u=unpin  q=close+reopen  ?=help
#include <Arduino.h>
#include <WiFi.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "esp_tls.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"

#include "anvil_endpoint.hpp"
#include "anvil_root_ca.hpp"
#include "secrets.h"

using depthcharge::fw::kAnvilRootCaPem;

// THE ENDPOINT IS SHARED NOW, NOT COPIED, and this file is the reason.
//
// These two used to be private constants right here, and on 2026-08-16 the
// shipping firmware's path gained `&depth=27` (backlog A7) while this copy did
// not. Nothing broke and nothing went red — this tool would simply have
// subscribed a ~3x heavier stream than the build it exists to be compared
// against, so its next autopsy would have measured the wrong stream and said so
// confidently. Found by the bench-acceptance review of that same change.
using depthcharge::fw::kAnvilHost;
using depthcharge::fw::kAnvilPath;

namespace {

constexpr char kBulkTlsHost[] = "speed.cloudflare.com";
constexpr char kBulkTlsPath[] = "/__down?bytes=1000000000";
constexpr char kBulkTcpHost[] = "ipv4.download.thinkbroadband.com";
constexpr char kBulkTcpPath[] = "/1GB.zip";

// The sick leg, as identified at the bench on 2026-08-13: the node board A's
// boot landed on at −68…−84 dBm while a −50 association stood beside it, and
// where every hole and socket death of the afternoon run lives. 'p' pins this
// board's association to it — putting the autopsy deliberately on the bad leg
// to collect death labels there — and 'u' releases it.
constexpr uint8_t kPinBssid[6] = {0xEE, 0xD3, 0x62, 0xAE, 0x81, 0xF9};
constexpr int32_t kPinChannel  = 4;

constexpr uint32_t kReadTimeoutMs   = 1000;   // SO_RCVTIMEO; a miss is not a death
constexpr uint32_t kStallDeathMs    = 15000;  // no bytes this long -> autopsy as stall
constexpr uint32_t kReopenDelayMs   = 500;
constexpr uint32_t kStatusPeriodMs  = 5000;
constexpr uint32_t kSummaryPeriodMs = 30000;

enum class Mode : uint8_t { AnvilWs = 0, TlsBulk = 1, TcpBulk = 2 };
const char* mode_name(Mode m) {
    switch (m) {
        case Mode::AnvilWs: return "anvil-ws";
        case Mode::TlsBulk: return "tls-bulk";
        default:            return "tcp-bulk";
    }
}

// --- association bookkeeping, written from the Wi-Fi event task --------------
volatile uint32_t g_sta_drops = 0;
void on_wifi_event(arduino_event_id_t id, arduino_event_info_t info) {
    const uint32_t now = millis();
    if (id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        ++g_sta_drops;
        Serial.printf("[%8lu] EVT STA DISCONNECT #%lu reason %u — the association layer DID drop\r\n",
                      (unsigned long)now, (unsigned long)g_sta_drops,
                      (unsigned)info.wifi_sta_disconnected.reason);
    } else if (id == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        Serial.printf("[%8lu] EVT got ip: %s rssi %d dBm\r\n", (unsigned long)now,
                      WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    }
}

// --- the flow under test ------------------------------------------------------
struct Flow {
    Mode mode = Mode::AnvilWs;
    bool open = false;
    esp_tls_t* tls = nullptr;   // TLS arms
    int fd = -1;                // TCP arm owns it; TLS arms borrow it for SO_ERROR
    uint32_t opened_ms = 0;
    uint32_t last_byte_ms = 0;
    uint64_t bytes = 0;
    uint32_t data_frames = 0, ctrl_frames = 0;
    // Worst silence between reads, per status window and per connection. The
    // 5 s status totals hide sub-window fades, and whether this board sees the
    // same 1-2 s silences the main firmware logs — or none at all — is what
    // separates "shared RF weather, survived" from "the other stack makes its
    // own holes". Added 2026-08-13 after the pinned A/B run needed exactly that.
    uint32_t worst_gap_window_ms = 0, worst_gap_conn_ms = 0;
    // WS parser state.
    enum class Ws : uint8_t { H1, H2, Ext, Payload } ws = Ws::H1;
    uint8_t opcode = 0, ext_need = 0;
    uint64_t remaining = 0;
    uint8_t ping_buf[125]; uint8_t ping_len = 0; uint32_t payload_seen = 0;
    int last_close_code = -1;
    char close_reason[64] = {0};
};
Flow g_flow;

// Death-cause tally. Keyed by short label so the 30 s summary reads at a
// glance; the per-death line carries the full numbers.
struct CauseCount { char label[16]; uint32_t count; };
CauseCount g_causes[12] = {};
uint32_t g_deaths = 0, g_connects = 0, g_connect_fails = 0;
struct DeathRow { uint32_t ms; uint32_t kb; char label[16]; };
DeathRow g_recent[10]; uint32_t g_recent_n = 0;

void count_cause(const char* label, uint32_t lifetime_ms, uint64_t bytes) {
    for (auto& c : g_causes) {
        if (c.count == 0 || strncmp(c.label, label, sizeof(c.label)) == 0) {
            strncpy(c.label, label, sizeof(c.label) - 1);
            ++c.count;
            break;
        }
    }
    DeathRow& r = g_recent[g_recent_n % 10];
    r.ms = lifetime_ms;
    r.kb = (uint32_t)(bytes / 1024);
    strncpy(r.label, label, sizeof(r.label) - 1);
    ++g_recent_n;
}

// The reason this sketch exists, in one function. `rc` is what the failing
// read/write returned: >0 impossible here, 0 = clean close, <0 = mbedtls (or
// -1 with errno for the plain-TCP arm). Classify, name, and record it.
void autopsy(const char* what, int rc, int saved_errno) {
    const uint32_t now = millis();
    const uint32_t lifetime = now - g_flow.opened_ms;

    int so_error = 0;
    socklen_t so_len = sizeof(so_error);
    if (g_flow.fd >= 0) { getsockopt(g_flow.fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len); }

    int tls_code = 0, tls_flags = 0;
    if (g_flow.tls != nullptr) {
        esp_tls_get_and_clear_last_error(g_flow.tls->error_handle, &tls_code, &tls_flags);
    }

    char mbed[96] = "n/a";
    if (rc < 0 && g_flow.mode != Mode::TcpBulk) { mbedtls_strerror(rc, mbed, sizeof(mbed)); }

    // `what` classifies before rc: stalls and connect failures arrive with
    // rc == 0, and letting the rc==0 arm run first filed every stall as
    // "clean-close" — the at-a-glance tally misreporting the primary
    // phenomenon (caught in the 2026-08-13 review; the rc-based arms are for
    // genuine read/write deaths only).
    const char* label = "other";
    if (strcmp(what, "stall") == 0)                        { label = "stall"; }
    else if (strcmp(what, "connect") == 0)                 { label = "connect-fail"; }
    else if (g_flow.last_close_code >= 0)                  { label = "ws-close"; }
    else if (rc == 0)                                      { label = "clean-close"; }
    else if (rc == -0x7280)                                { label = "tcp-fin"; }  // EOF with no close_notify: a polite FIN, not clean TLS
    else if (saved_errno == ECONNRESET || rc == -0x0050)   { label = "tcp-rst"; }
    else if (rc == -0x7180)                                { label = "bad-mac"; }
    else if (rc == -0x7200)                                { label = "bad-record"; }
    else if (rc == -0x7780)                                { label = "fatal-alert"; }
    else if (rc == -0x7F00 || saved_errno == ENOMEM)       { label = "alloc"; }

    ++g_deaths;
    count_cause(label, lifetime, g_flow.bytes);
    Serial.printf("[%8lu] DEATH #%lu %s [%s] in %s: lifetime %lu ms, %llu bytes,"
                  " %lu data / %lu ctrl frames\r\n",
                  (unsigned long)now, (unsigned long)g_deaths, mode_name(g_flow.mode), label,
                  what, (unsigned long)lifetime, (unsigned long long)g_flow.bytes,
                  (unsigned long)g_flow.data_frames, (unsigned long)g_flow.ctrl_frames);
    Serial.printf("[%8lu]   rc=%s0x%04X (%s) errno=%d (%s) so_error=%d esp_tls=0x%X/0x%X"
                  " rssi=%d dBm sta_drops=%lu\r\n",
                  (unsigned long)now, rc < 0 ? "-" : "", (unsigned)(rc < 0 ? -rc : rc), mbed,
                  saved_errno, strerror(saved_errno), so_error, (unsigned)tls_code,
                  (unsigned)tls_flags, (int)WiFi.RSSI(), (unsigned long)g_sta_drops);
    if (g_flow.last_close_code >= 0) {
        Serial.printf("[%8lu]   ws close frame: code %d reason '%s' <- the server SAID why\r\n",
                      (unsigned long)now, g_flow.last_close_code, g_flow.close_reason);
    }
}

void close_flow() {
    if (g_flow.tls != nullptr) { esp_tls_conn_destroy(g_flow.tls); g_flow.tls = nullptr; }
    else if (g_flow.fd >= 0)   { ::close(g_flow.fd); }
    g_flow.fd = -1;
    g_flow.open = false;
}

void set_rcvtimeo(int fd) {
    timeval tv{};
    tv.tv_sec = kReadTimeoutMs / 1000;
    tv.tv_usec = (kReadTimeoutMs % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// Returns false (with autopsy already printed) on any failure.
bool open_flow(Mode mode) {
    g_flow = Flow{};
    g_flow.mode = mode;
    g_flow.opened_ms = millis();

    const char* host = (mode == Mode::AnvilWs) ? kAnvilHost
                     : (mode == Mode::TlsBulk) ? kBulkTlsHost : kBulkTcpHost;
    const uint16_t port = (mode == Mode::TcpBulk) ? 80 : 443;
    const uint32_t t0 = millis();

    if (mode == Mode::TcpBulk) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (::getaddrinfo(host, "80", &hints, &res) != 0 || res == nullptr) {
            autopsy("connect", -1, errno);
            return false;
        }
        g_flow.fd = ::socket(AF_INET, SOCK_STREAM, 0);
        reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_port = htons(port);
        const int rc = ::connect(g_flow.fd, res->ai_addr, res->ai_addrlen);
        ::freeaddrinfo(res);
        if (rc != 0) { autopsy("connect", rc, errno); close_flow(); return false; }
    } else {
        esp_tls_cfg_t cfg = {};
        cfg.timeout_ms = 10000;
        if (mode == Mode::AnvilWs) {
            // The production trust anchor, so arm 1 is the production path.
            cfg.cacert_buf = reinterpret_cast<const unsigned char*>(kAnvilRootCaPem);
            cfg.cacert_bytes = sizeof(kAnvilRootCaPem);  // PEM: NUL included
        }
        // Arm 2 sets no CA at all: esp-tls logs one warning and skips
        // verification, which is fine — trust is not what is under test.
        g_flow.tls = esp_tls_init();
        if (g_flow.tls == nullptr) { autopsy("connect", -1, ENOMEM); return false; }
        if (esp_tls_conn_new_sync(host, strlen(host), port, &cfg, g_flow.tls) != 1) {
            autopsy("connect", -1, errno);
            close_flow();
            return false;
        }
        (void)esp_tls_get_conn_sockfd(g_flow.tls, &g_flow.fd);
    }
    set_rcvtimeo(g_flow.fd);

    // The request. Arm 1 is a WebSocket upgrade; the bulk arms are plain GETs.
    char req[320];
    if (mode == Mode::AnvilWs) {
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
                 "Connection: Upgrade\r\nSec-WebSocket-Key: ZGlhZ25vc3RpYy1rZXktMQ==\r\n"
                 "Sec-WebSocket-Version: 13\r\n\r\n",
                 kAnvilPath, host);
    } else {
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: depthcharge-link-autopsy\r\n"
                 "Accept: */*\r\nConnection: keep-alive\r\n\r\n",
                 (mode == Mode::TlsBulk) ? kBulkTlsPath : kBulkTcpPath, host);
    }
    const int wlen = (int)strlen(req);
    const int wrc = (mode == Mode::TcpBulk)
        ? (int)::send(g_flow.fd, req, wlen, 0)
        : (int)esp_tls_conn_write(g_flow.tls, req, wlen);
    if (wrc != wlen) { autopsy("request-write", wrc, errno); close_flow(); return false; }

    // Arm 1: confirm the 101 before treating bytes as frames. Headers are
    // consumed byte-accurately, so anything after \r\n\r\n stays for the parser.
    if (mode == Mode::AnvilWs) {
        char hdr[512];
        size_t n = 0;
        const uint32_t deadline = millis() + 8000;
        while (millis() < deadline && n < sizeof(hdr) - 1) {
            const int r = (int)esp_tls_conn_read(g_flow.tls, hdr + n, 1);
            if (r == 1) {
                ++n;
                if (n >= 4 && memcmp(hdr + n - 4, "\r\n\r\n", 4) == 0) { break; }
            } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE && r <= 0) {
                hdr[n] = 0;
                autopsy("upgrade-read", r, errno);
                close_flow();
                return false;
            }
        }
        hdr[n] = 0;
        if (strstr(hdr, " 101 ") == nullptr) {
            Serial.printf("[%8lu] upgrade REFUSED: %.120s\r\n", (unsigned long)millis(), hdr);
            close_flow();
            return false;
        }
    }

    ++g_connects;
    g_flow.open = true;
    g_flow.last_byte_ms = millis();
    Serial.printf("[%8lu] OPEN #%lu %s to %s:%u in %lu ms (fd %d, rssi %d dBm)\r\n",
                  (unsigned long)millis(), (unsigned long)g_connects, mode_name(mode), host,
                  (unsigned)port, (unsigned long)(millis() - t0), g_flow.fd, (int)WiFi.RSSI());
    return true;
}

// Frame completion, in ONE place so it runs for every frame end — including
// zero-length frames, which complete in the H2 and Ext states without ever
// entering Payload. The first draft kept completion in the Payload state only,
// which silently skipped two cases the 2026-08-13 review caught: an EMPTY ping
// (the common spelling — esp_websocket_client itself sends them) was never
// ponged, so a pong-enforcing server would kill the flow and this instrument
// would autopsy a death it caused; and an empty close (the widespread
// no-status close) never set last_close_code, filing a deliberate close as
// "clean-close" with no code.
void finish_frame() {
    (g_flow.opcode >= 0x8) ? ++g_flow.ctrl_frames : ++g_flow.data_frames;
    if (g_flow.opcode == 0x9) {
        // Pong, masked as the RFC requires of clients; an all-zero key is a
        // legal mask and leaves the payload bytes unchanged. A zero-length
        // pong answers a zero-length ping.
        uint8_t pong[2 + 4 + 125];
        pong[0] = 0x8A;
        pong[1] = 0x80 | g_flow.ping_len;
        memset(pong + 2, 0, 4);
        memcpy(pong + 6, g_flow.ping_buf, g_flow.ping_len);
        const int want = 6 + (int)g_flow.ping_len;
        if ((int)esp_tls_conn_write(g_flow.tls, pong, want) != want) {
            // A short pong desyncs the WS byte stream and the server's
            // protocol-error close would read as a server-side kill — say so.
            Serial.printf("[%8lu] pong write short/failed — stream may desync\r\n",
                          (unsigned long)millis());
        }
    } else if (g_flow.opcode == 0x8 && g_flow.payload_seen == 0) {
        g_flow.last_close_code = 1005;  // RFC 6455: close frame with no status
    }
}

// Minimal server->client WS frame walk: counts frames, answers pings, and
// captures the close frame's code + reason — the one message a
// policy-enforcing server sends that esp_websocket_client never showed us.
void ws_parse(const uint8_t* p, int n) {
    for (int i = 0; i < n; ++i) {
        const uint8_t b = p[i];
        switch (g_flow.ws) {
            case Flow::Ws::H1:
                g_flow.opcode = b & 0x0F;
                g_flow.ws = Flow::Ws::H2;
                break;
            case Flow::Ws::H2: {
                const uint8_t len7 = b & 0x7F;  // server frames are unmasked
                g_flow.payload_seen = 0;
                g_flow.ping_len = 0;
                if (len7 < 126)       { g_flow.remaining = len7; g_flow.ext_need = 0; }
                else if (len7 == 126) { g_flow.remaining = 0; g_flow.ext_need = 2; }
                else                  { g_flow.remaining = 0; g_flow.ext_need = 8; }
                g_flow.ws = (g_flow.ext_need > 0) ? Flow::Ws::Ext
                          : (g_flow.remaining > 0) ? Flow::Ws::Payload : Flow::Ws::H1;
                if (g_flow.ws == Flow::Ws::H1) { finish_frame(); }  // empty frame
                break;
            }
            case Flow::Ws::Ext:
                g_flow.remaining = (g_flow.remaining << 8) | b;
                if (--g_flow.ext_need == 0) {
                    g_flow.ws = (g_flow.remaining > 0) ? Flow::Ws::Payload : Flow::Ws::H1;
                    if (g_flow.ws == Flow::Ws::H1) { finish_frame(); }  // empty frame, long form
                }
                break;
            case Flow::Ws::Payload: {
                if (g_flow.opcode == 0x8) {  // close: code, then reason text
                    if (g_flow.payload_seen < 2) {
                        g_flow.last_close_code = (g_flow.payload_seen == 0)
                            ? (b << 8) : ((g_flow.last_close_code & 0xFF00) | b);
                    } else if (g_flow.payload_seen - 2 < sizeof(g_flow.close_reason) - 1) {
                        g_flow.close_reason[g_flow.payload_seen - 2] = (char)b;
                    }
                } else if (g_flow.opcode == 0x9 && g_flow.ping_len < sizeof(g_flow.ping_buf)) {
                    g_flow.ping_buf[g_flow.ping_len++] = b;
                }
                ++g_flow.payload_seen;
                if (--g_flow.remaining == 0) {
                    finish_frame();
                    g_flow.ws = Flow::Ws::H1;
                }
                break;
            }
        }
    }
}

void print_summary() {
    const uint32_t now = millis();
    Serial.printf("[%8lu] -- summary: mode=%s connects=%lu deaths=%lu sta_drops=%lu rssi=%d dBm\r\n",
                  (unsigned long)now, mode_name(g_flow.mode), (unsigned long)g_connects,
                  (unsigned long)g_deaths, (unsigned long)g_sta_drops, (int)WiFi.RSSI());
    Serial.printf("[%8lu] -- causes:", (unsigned long)now);
    for (const auto& c : g_causes) {
        if (c.count > 0) { Serial.printf(" %s x%lu", c.label, (unsigned long)c.count); }
    }
    Serial.print("\r\n");
    if (g_recent_n > 0) {
        Serial.printf("[%8lu] -- last deaths (ms/KB/cause):", (unsigned long)now);
        const uint32_t shown = (g_recent_n < 10) ? g_recent_n : 10;
        for (uint32_t i = 0; i < shown; ++i) {
            const DeathRow& r = g_recent[(g_recent_n - shown + i) % 10];
            Serial.printf(" %lu/%lu/%s", (unsigned long)r.ms, (unsigned long)r.kb, r.label);
        }
        Serial.print("\r\n");
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);  // the USB-serial link enumerates after boot

    Serial.print("\r\n[       0] link-autopsy: how, exactly, does the socket die?\r\n"
                 "[       0] commands: 1=anvil-ws  2=tls-bulk  3=tcp-bulk  p=pin-sick-node"
                 "  u=unpin  q=close+reopen  ?=help\r\n");

    WiFi.onEvent(on_wifi_event);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // mirror the main firmware: set after mode(), before begin()
    WiFi.begin(kWifiSsid, kWifiPassword);
}

void loop() {
    static uint32_t last_status = 0, last_summary = 0, reopen_at = 0, wifi_down_ms = 0;
    static uint8_t buf[4096];
    const uint32_t now = millis();

    while (Serial.available() > 0) {
        const int c = Serial.read();
        Mode next = g_flow.mode;
        if (c == '1') { next = Mode::AnvilWs; }
        else if (c == '2') { next = Mode::TlsBulk; }
        else if (c == '3') { next = Mode::TcpBulk; }
        else if (c == 'q') { if (g_flow.open) { close_flow(); } reopen_at = now; continue; }
        else if (c == 'p' || c == 'u') {
            // Re-associate, pinned to the sick node or released to the driver's
            // choice. The flow dies with the association; not counted as a death.
            if (g_flow.open) { close_flow(); }
            reopen_at = now + 2000;
            WiFi.disconnect();
            if (c == 'p') {
                Serial.printf("[%8lu] PINNING to %02X:%02X:%02X:%02X:%02X:%02X ch %d\r\n",
                              (unsigned long)now, kPinBssid[0], kPinBssid[1], kPinBssid[2],
                              kPinBssid[3], kPinBssid[4], kPinBssid[5], (int)kPinChannel);
                WiFi.begin(kWifiSsid, kWifiPassword, kPinChannel, kPinBssid);
            } else {
                Serial.printf("[%8lu] UNPINNED — driver picks the node\r\n", (unsigned long)now);
                WiFi.begin(kWifiSsid, kWifiPassword);
            }
            continue;
        }
        else if (c == '?') {
            Serial.print("1=anvil-ws  2=tls-bulk  3=tcp-bulk  p=pin-sick-node  u=unpin"
                         "  q=close+reopen  ?=help\r\n");
            continue;
        } else { continue; }
        if (g_flow.open) { close_flow(); }
        g_flow.mode = next;
        reopen_at = now;
        Serial.printf("[%8lu] mode -> %s\r\n", (unsigned long)now, mode_name(next));
    }

    if (WiFi.status() != WL_CONNECTED) {
        if (g_flow.open) {
            // Not a death of the flow — the floor under it moved. Recorded
            // apart so flow-kill statistics stay clean.
            Serial.printf("[%8lu] association lost UNDER the flow — closing (not counted)\r\n",
                          (unsigned long)now);
            close_flow();
        }
        if (wifi_down_ms == 0) { wifi_down_ms = now; }
        if (now - wifi_down_ms > 10000) {  // Arduino gives up on some deauths
            wifi_down_ms = 0;
            WiFi.disconnect();
            WiFi.begin(kWifiSsid, kWifiPassword);
        }
        delay(100);
        return;
    }
    wifi_down_ms = 0;

    if (!g_flow.open) {
        if (now >= reopen_at + kReopenDelayMs) {
            const Mode m = g_flow.mode;
            if (!open_flow(m)) {
                ++g_connect_fails;
                g_flow.mode = m;
                reopen_at = millis();
            }
        } else {
            delay(20);
        }
        return;
    }

    // The read. A timeout (WANT_READ / EWOULDBLOCK) is not a death — Anvil
    // pauses are real — but silence past kStallDeathMs is autopsied as one,
    // because that is the wd-gap shape from the main firmware's log.
    int rc;
    if (g_flow.mode == Mode::TcpBulk) {
        rc = (int)::recv(g_flow.fd, buf, sizeof(buf), 0);
        if (rc < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) { rc = INT32_MIN; }
    } else {
        rc = (int)esp_tls_conn_read(g_flow.tls, buf, sizeof(buf));
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) { rc = INT32_MIN; }
    }

    if (rc > 0) {
        // Re-sampled: the read above can block up to kReadTimeoutMs, and the
        // gap instrument measures effects of the same order — stamping with
        // the pre-read `now` made both gap endpoints up to ~1 s early.
        const uint32_t at = millis();
        const uint32_t gap = at - g_flow.last_byte_ms;
        if (gap > g_flow.worst_gap_window_ms) { g_flow.worst_gap_window_ms = gap; }
        if (gap > g_flow.worst_gap_conn_ms)   { g_flow.worst_gap_conn_ms = gap; }
        g_flow.bytes += (uint64_t)rc;
        g_flow.last_byte_ms = at;
        if (g_flow.mode == Mode::AnvilWs) { ws_parse(buf, rc); }
        if (g_flow.last_close_code >= 0) {  // server said goodbye in-band
            autopsy("ws-close-frame", 0, 0);
            close_flow();
            reopen_at = millis();
        }
    } else if (rc == INT32_MIN) {  // quiet tick
        if (now - g_flow.last_byte_ms > kStallDeathMs) {
            autopsy("stall", 0, 0);
            close_flow();
            reopen_at = millis();
        }
    } else {  // rc <= 0: the event this sketch exists to catch
        // Unless the floor moved under it: the association can drop inside
        // the blocking read (WiFi.status() was checked before it), in which
        // case lwIP aborts the pcb and the read error is the association's,
        // not the flow's — route it to the not-counted path the same way the
        // pre-read check does, so the cause tally stays clean.
        const int read_errno = errno;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("[%8lu] association fell during the read — closing (not counted)\r\n",
                          (unsigned long)millis());
            close_flow();
        } else {
            autopsy(rc == 0 ? "clean-close" : "read", rc, read_errno);
            close_flow();
        }
        reopen_at = millis();
    }

    if (now - last_status >= kStatusPeriodMs && g_flow.open) {
        last_status = now;
        Serial.printf("[%8lu] %s up %lu s: %llu KB, %lu data / %lu ctrl frames, rssi %d dBm,"
                      " gap %lu ms (conn worst %lu)\r\n",
                      (unsigned long)now, mode_name(g_flow.mode),
                      (unsigned long)((now - g_flow.opened_ms) / 1000),
                      (unsigned long long)(g_flow.bytes / 1024), (unsigned long)g_flow.data_frames,
                      (unsigned long)g_flow.ctrl_frames, (int)WiFi.RSSI(),
                      (unsigned long)g_flow.worst_gap_window_ms,
                      (unsigned long)g_flow.worst_gap_conn_ms);
        g_flow.worst_gap_window_ms = 0;
    }
    if (now - last_summary >= kSummaryPeriodMs) {
        last_summary = now;
        print_summary();
    }
}
