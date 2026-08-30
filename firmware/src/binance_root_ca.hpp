// firmware/src/binance_root_ca.hpp — the TLS trust anchor for Binance.
//
// The third of these, and it exists for the reason the first two do: the IDF
// 4.4 vintage of esp-tls that ships with Arduino-ESP32 2.0.14 exposes
// `cert_pem` and `use_global_ca_store` and no `crt_bundle_attach`, so the
// choice is not "bundle vs pin" — it is "pin, or move to pure ESP-IDF and
// re-run M2's first light". See anvil_root_ca.hpp for the full argument; it is
// not repeated here.
//
// ===========================================================================
// THE QUESTION THIS STAGE WAS TOLD TO MEASURE: ONE ANCHOR OR TWO?
// ===========================================================================
//
// Binance splits its market data across two hosts — `data-stream.binance.vision`
// for the diff stream and `data-api.binance.vision` for the REST seed — and the
// D-A1 brief would not let that be assumed either way: *"Both under
// binance.vision, so whether that is one anchor or two is a measurement — M4's
// precedent is to take it off the live server twice."*
//
// **Taken off both live servers on 2026-08-27. It is ONE anchor, and the reason
// is stronger than a shared root: it is a shared LEAF.**
//
//     data-stream.binance.vision   leaf sha1 EC:52:46:B9:BE:11:53:F3:7E:BF:
//                                            68:68:40:50:DF:80:72:16:E3:A3
//     data-api.binance.vision      leaf sha1 EC:52:46:B9:BE:11:53:F3:7E:BF:
//                                            68:68:40:50:DF:80:72:16:E3:A3
//
// The same certificate, byte for byte, because it is a wildcard:
// `subjectAltName = DNS:*.binance.vision, DNS:binance.vision`. Both hosts then
// present the same intermediate and the same root. So this file is the anchor
// for both, and D-A2's REST client needs no second PEM and no second decision.
//
// WHAT THE SERVERS ACTUALLY PUT ON THE WIRE, read with `-showcerts` and no
// trust store consulted, because mbedtls on the board sees this and nothing
// else:
//
//     [0] CN=*.binance.vision                issuer Amazon RSA 2048 M01
//         2026-08-18 .. 2027-03-03
//     [1] C=US, O=Amazon, CN=Amazon RSA 2048 M01
//         issuer Amazon Root CA 1            2022-08-23 .. 2030-08-23
//     [2] C=US, O=Amazon, CN=Amazon Root CA 1
//         issuer ...Starfield Services Root Certificate Authority - G2
//         2015-05-25 .. 2037-12-31           <-- CROSS-SIGNED
//
// So, exactly as at Kraken and at Anvil, the server presents leaf +
// intermediate + a **cross-signed** root, and there are two anchors that would
// validate: Starfield Services Root CA G2, or Amazon Root CA 1.
//
// WHY AMAZON ROOT CA 1, AND IN ITS SELF-SIGNED FORM. Pinning either validates
// the presented chain — mbedtls stops at the first certificate whose subject
// and key match a trusted one — so the tiebreak is lifetime, and M4's precedent
// is to prefer the longer-lived self-signed root:
//
//     Starfield Services Root CA G2        expires 2037-12-31
//     Amazon Root CA 1  (cross-signed)     expires 2037-12-31
//     Amazon Root CA 1  (self-signed)      expires 2038-01-17   <-- below
//
// The self-signed R1 is NOT the certificate the server sends; the cross-signed
// form is. Pinning the self-signed one still works, and this is the same
// substitution `kraken_root_ca.hpp` makes for GTS Root R4 for the same reason:
// the intermediate's issuer name matches the self-signed root's subject and its
// signature verifies under the same key, so the chain terminates one link
// earlier and cert [2] is simply unused.
//
//     subject = C=US, O=Amazon, CN=Amazon Root CA 1
//     issuer  = C=US, O=Amazon, CN=Amazon Root CA 1   (self-signed)
//     valid   = 2015-05-26 .. 2038-01-17
//     sha1    = 8D:A7:F9:65:EC:5E:FC:37:91:0F:1C:6E:59:FD:C1:CC:6A:6E:DE:16
//
// VERIFIED RATHER THAN ASSERTED — and this is the check that matters, because
// everything above is a description and this is a test. Both hosts were
// re-connected to with this PEM as the ONLY trust anchor and with
// `-verify_return_error` set, so a failure could not be reported as success:
//
// A BLOCK COMMENT FOR THE COMMAND, and not a stylistic choice: a trailing
// backslash on a `//` line is a line-continuation, so the lines below were ONE
// comment and -Wcomment says so. It went unseen until M5 stage D-A3 added
// `dc_tests_binance`: this header had never been compiled by the host suite at
// all, which is the gap that target exists to close.
/*
   openssl s_client -connect data-stream.binance.vision:443 \
   -servername data-stream.binance.vision \
   -CAfile AmazonRootCA1.pem -verify_return_error
   ...  Verify return code: 0 (ok)

   (and identically for data-api.binance.vision)
*/
//
// THE LEAF IS NOT PINNED, and that is deliberate rather than an omission. It
// expires 2027-03-03 and Binance rotates it; pinning a leaf would turn a
// routine rotation into a board that cannot connect. The root is the anchor at
// all three venues for the same reason.
#pragma once

namespace depthcharge::fw {

// Amazon Root CA 1, self-signed form. Obtained from Amazon's published
// repository (`https://www.amazontrust.com/repository/AmazonRootCA1.pem`) and
// then verified against the live servers as above — the verification is what
// makes the source unimportant, which is the point of doing it that way round.
inline constexpr char kBinanceRootCaPem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
    "-----END CERTIFICATE-----\n";

}  // namespace depthcharge::fw
