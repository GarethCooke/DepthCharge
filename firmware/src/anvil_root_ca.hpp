// firmware/src/anvil_root_ca.hpp — the TLS trust anchor for anvil.garethcooke.com.
//
// WHY A PINNED ROOT AND NOT esp_crt_bundle.
//
// The M3 brief asks for the ESP-IDF certificate bundle, and it is present and
// enabled in this framework (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y, DEFAULT_FULL,
// 200 certs). It is nonetheless unreachable from here: the bundle is attached
// through esp_tls_cfg_t::crt_bundle_attach, and the IDF 4.4 vintage of
// esp_websocket_client_config_t that ships with Arduino-ESP32 2.0.14 exposes no
// such field — it offers cert_pem, use_global_ca_store and nothing else. So the
// choice is not "bundle vs pin", it is "pin, or move to pure ESP-IDF and re-run
// M2's first light". The brief's own known-unknown anticipates this: "if the
// handshake fails, pin the specific CA and note why. Record the chain and the
// choice."
//
// THE CHAIN, measured against the live server on 2026-08-08 rather than assumed:
//
//     0  CN=anvil.garethcooke.com          issued by  Let's Encrypt YE1
//     1  Let's Encrypt YE1                 issued by  ISRG Root YE
//     2  ISRG Root YE                      issued by  ISRG Root X2
//     3  ISRG Root X2                      issued by  ISRG Root X1   <-- anchor
//
// The server presents four certificates, ending with ISRG Root X2 *cross-signed*
// by ISRG Root X1. Pinning X1 therefore validates the whole chain, and it is the
// stable choice: X1 is a long-lived RSA root valid until 2035-06-04, where the
// leaf rotates every ~90 days and the intermediates every few years.
//
// Verified before it was committed, with a control:
//     openssl verify -CAfile isrg_x1.pem -untrusted <chain> <leaf>   -> OK
//     openssl verify -CAfile <unrelated self-signed> ... -> error 20 at depth 3
// so the pass means the anchor is doing the work, not that verification was off.
//
// PROVENANCE. ISRG Root X1, SHA-256 fingerprint
//   96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6
// Check it before trusting this file:
//   openssl x509 -in <this PEM> -noout -fingerprint -sha256 -enddate -subject
//
// WHEN THIS BREAKS. Two ways, both loud rather than silent: the root expires in
// 2035, or Anvil moves to a CA outside the ISRG hierarchy. Either shows up as a
// TLS handshake failure on the serial log at connect, never as a wrong ladder.
// The fix in both cases is to replace the PEM below, or to move the firmware to
// pure ESP-IDF and use the bundle properly.
#pragma once

namespace depthcharge::fw {

// ISRG Root X1 — the anchor above the whole Let's Encrypt hierarchy.
// PEM, NUL-terminated, so esp_websocket_client_config_t::cert_len stays 0.
inline constexpr char kAnvilRootCaPem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
    "TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
    "cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
    "WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
    "ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
    "MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
    "h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
    "0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
    "A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
    "T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
    "B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
    "B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
    "KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
    "OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
    "jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
    "qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
    "rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
    "HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
    "hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
    "ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
    "3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
    "NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
    "ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
    "TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
    "jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
    "oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
    "4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
    "mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
    "emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
    "-----END CERTIFICATE-----\n";

}  // namespace depthcharge::fw
