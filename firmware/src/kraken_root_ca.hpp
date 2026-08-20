// firmware/src/kraken_root_ca.hpp — the TLS trust anchor for ws.kraken.com.
//
// The Kraken counterpart of anvil_root_ca.hpp, and it exists for the same
// reason that file does: the IDF 4.4 vintage of esp-tls that ships with
// Arduino-ESP32 2.0.14 exposes `cert_pem` and `use_global_ca_store` and no
// `crt_bundle_attach`, so the choice is not "bundle vs pin" — it is "pin, or
// move to pure ESP-IDF and re-run M2's first light". See anvil_root_ca.hpp for
// the full argument; it is not repeated here.
//
// THE CHAIN, MEASURED AGAINST THE LIVE SERVER ON 2026-08-20 RATHER THAN
// ASSUMED, and measured twice by two different methods because the second one
// answers a question the first cannot.
//
// What the operating system's verifier reports (it may fill in issuers from its
// own store, so this is the chain that VALIDATES):
//
//     0  CN=ws.kraken.com     issued by  WE1 (Google Trust Services)
//     1  WE1                  issued by  GTS Root R4
//     2  GTS Root R4          issued by  GlobalSign Root CA     <-- cross-signed
//     3  GlobalSign Root CA   self-signed
//
// What the SERVER actually puts on the wire, read out of a hand-parsed TLS 1.2
// Certificate handshake message with no trust store consulted at all — because
// mbedtls on the board sees this and nothing else:
//
//     [0] 884 B  sha1 6982AB9F...  CN=ws.kraken.com,  issuer WE1
//     [1] 675 B  sha1 108FBF79...  CN=WE1,            issuer GTS Root R4
//     [2] 894 B  sha1 932BED33...  CN=GTS Root R4,    issuer GlobalSign Root CA
//
// So the server presents leaf + intermediate + a cross-signed root: the same
// shape Anvil presents, and there are two anchors that would work.
//
// WHY GTS ROOT R4 AND NOT GLOBALSIGN. Pinning either validates the presented
// chain — mbedtls stops at the first certificate whose subject and key match a
// trusted one — so the tiebreak is lifetime, and it is not close:
//
//     GlobalSign Root CA   expires 2028-01-28   (17 months from this commit)
//     GTS Root R4          expires 2036-06-22   (self-signed form, below)
//
// The self-signed R4 is not the certificate the server sends; the cross-signed
// one is, and the cross-signed one expires with GlobalSign. Both carry the SAME
// P-384 public key, which is the fact the pin actually rests on and is therefore
// the fact that was checked rather than assumed: the 97-byte uncompressed point
// from the self-signed R4 below was searched for, byte for byte, inside each of
// the three certificates the server sent, and found in exactly one — [2], the
// cross-signed R4. It is absent from the leaf and from WE1, so the match is not
// a coincidence of common encoding. That makes WE1's signature verifiable
// against the key below, which is the whole of what the handshake needs.
//
// PROVENANCE. GTS Root R4, SHA-256 fingerprint
//   71:CC:A5:39:1F:9E:79:4B:04:80:25:30:B3:63:E1:21:DA:8A:30:43:BB:26:66:2F:EA:4D:CA:7F:C9:51:A4:BD
// Taken from this desk's own Windows root store rather than downloaded, so the
// bytes came from a store the machine already trusts for every TLS connection
// it makes. Check it before trusting this file:
//   openssl x509 -in <this PEM> -noout -fingerprint -sha256 -enddate -subject
//
// IT IS AN ECDSA P-384 ROOT, WHERE ANVIL'S IS RSA-4096. Worth stating because
// it is the first ECC anchor in this firmware: mbedtls needs
// MBEDTLS_ECDSA_C + MBEDTLS_ECP_DP_SECP384R1_ENABLED, both of which are on in
// the stock ESP-IDF 4.4 config this framework ships. If the handshake fails
// with a "feature unavailable" rather than a verification error, that is the
// first thing to check — and it fails at connect on the serial log, never as a
// wrong ladder.
//
// WHEN THIS BREAKS. The root expires in 2036, or Kraken moves off the Google
// Trust Services hierarchy. Either is a TLS handshake failure at connect, which
// the autopsy line already prints.
#pragma once

namespace depthcharge::fw {

// GTS Root R4, self-signed form — the long-lived anchor above WE1.
// PEM, NUL-terminated, so the caller's cert_len stays 0.
inline constexpr char kKrakenRootCaPem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICCjCCAZGgAwIBAgIQbkepyIuUtui7OyrYorLBmTAKBggqhkjOPQQDAzBHMQsw\n"
    "CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU\n"
    "MBIGA1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw\n"
    "MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp\n"
    "Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQA\n"
    "IgNiAATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzu\n"
    "hXyiQHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/l\n"
    "xKvRHYqjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1Ud\n"
    "DgQWBBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNnADBkAjBqUFJ0\n"
    "CMRw3J5QdCHojXohw0+WbhXRIjVhLfoIN+4Zba3bssx9BzT1YBkstTTZbyACMANx\n"
    "sbqjYAuG7ZoIapVon+Kz4ZNkfF6Tpt95LY2F45TPI11xzPKwTdb+mciUqXWi4w==\n"
    "-----END CERTIFICATE-----\n";

}  // namespace depthcharge::fw
