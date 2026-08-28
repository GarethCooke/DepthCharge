// firmware/src/http_response.hpp — the HTTP/1.1 response reader, on the desk.
//
// M5 stage D-A2. ESP-IDF-free and host-tested, and it is lifted out of
// `rest_fetch.cpp` for exactly the reason `ws_frame.hpp` was lifted out of
// `ws_transport.cpp`: **the parsing is the part that can be wrong, and it is
// the part a desk can check.** What is left in the `.cpp` is four esp-tls calls
// and a socket, which a desk cannot check at all.
//
// It is fed ARBITRARY CHUNKS, because that is what a non-blocking socket
// delivers. `feed()` may be called with one byte or nine hundred, may be called
// with a chunk that ends mid-header, and must give the same answer either way.
// That property is the whole reason this is a state machine rather than a
// function: `ws_frame.hpp` makes the same argument for the same reason, and the
// M3 transport bug it was written after — `upgrade headers did not end in 512
// bytes` — was a header reader that assumed one read.
//
// WHAT IT DELIBERATELY DOES NOT DO:
//
//   * **Chunked transfer-encoding.** Refused loudly rather than mis-parsed. A
//     chunked body read as flat hands the JSON parser hex length prefixes
//     embedded mid-document: a parse error if we are lucky and a wrong book if
//     we are not. Binance sends Content-Length for `/api/v3/depth`; if that
//     ever changes this fails on the first fetch and says which header did it.
//   * **Keep-alive / pipelining.** The request sends `Connection: close`, so one
//     response per session and no remainder to carry.
//   * **Allocate.** It writes into a buffer the caller owns (invariant #7).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace depthcharge::fw {

// How far the reader has got. Ordered so `>= Body` means "headers are done".
enum class HttpPhase : std::uint8_t {
    Status = 0,   // reading the "HTTP/1.1 200 OK" line
    Headers,      // reading header lines until the blank one
    Body,         // copying Content-Length bytes
    Complete,     // the whole body is in the caller's buffer
    Failed,       // see HttpError
};

enum class HttpError : std::uint8_t {
    None = 0,
    BadStatusLine,   // no "HTTP/x.y NNN"
    NotOk,           // a status this client will not act on; see `status()`
    Chunked,         // transfer-encoding: chunked - refused, never mis-parsed
    NoLength,        // neither Content-Length nor chunked: body is unbounded
    TooBig,          // Content-Length exceeds the caller's buffer - REFUSED
    HeaderTooLong,   // a single header line longer than the line buffer
    Overrun,         // the server sent more body than it declared
};

// The longest single header line accepted. Sized like `kUpgradeHeaderBytes`
// was, by measurement rather than by guess: the largest single line in a
// Binance `/api/v3/depth` response is the `Date`/`Server`/CDN set, all well
// under 200 bytes, and Cloudflare's `set-cookie` at Kraken measured ~200 chars.
// 512 is 2.5x that, and a line longer than this is a header nobody expected
// rather than a buffer to grow.
inline constexpr std::size_t kHttpLineMax = 512;

class HttpResponse {
public:
    // `body` is the caller's buffer; the reader never allocates and never
    // writes past `body_cap`.
    void begin(char* body, std::size_t body_cap) noexcept {
        body_ = body;
        body_cap_ = body_cap;
        phase_ = HttpPhase::Status;
        error_ = HttpError::None;
        status_ = 0;
        content_length_ = 0;
        have_length_ = false;
        chunked_ = false;
        body_got_ = 0;
        line_len_ = 0;
    }

    // Consume `n` bytes. Returns the phase after consuming them. Any bytes
    // beyond the declared body length are an `Overrun` rather than silently
    // dropped: a server sending more than it promised is a server we have
    // misunderstood, and continuing would mean trusting the rest of it.
    HttpPhase feed(const char* p, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            if (phase_ == HttpPhase::Complete || phase_ == HttpPhase::Failed) {
                if (phase_ == HttpPhase::Complete) { return fail(HttpError::Overrun); }
                return phase_;
            }
            if (phase_ == HttpPhase::Body) {
                // Bulk-copy the rest of this chunk rather than one byte at a
                // time: the body is ~64 KB and the per-byte loop above is for
                // header text, which is ~300 bytes once.
                const std::size_t want = content_length_ - body_got_;
                std::size_t take = n - i;
                if (take > want) { take = want; }
                for (std::size_t k = 0; k < take; ++k) { body_[body_got_ + k] = p[i + k]; }
                body_got_ += take;
                i += take;
                if (body_got_ >= content_length_) { phase_ = HttpPhase::Complete; }
                if (i < n) {
                    // Bytes left over after the body is complete.
                    return fail(HttpError::Overrun);
                }
                return phase_;
            }
            const char c = p[i];
            if (c == '\r') { continue; }
            if (c != '\n') {
                if (line_len_ + 1 >= kHttpLineMax) { return fail(HttpError::HeaderTooLong); }
                line_[line_len_++] = c;
                continue;
            }
            line_[line_len_] = '\0';
            const bool blank = line_len_ == 0;
            const std::size_t len = line_len_;
            line_len_ = 0;
            if (phase_ == HttpPhase::Status) {
                if (!parse_status(line_, len)) { return fail(HttpError::BadStatusLine); }
                phase_ = HttpPhase::Headers;
                continue;
            }
            if (!blank) {
                note_header(line_, len);
                continue;
            }
            // The blank line: headers are done, decide whether a body can be read.
            if (chunked_) { return fail(HttpError::Chunked); }
            if (status_ != 200) { return fail(HttpError::NotOk); }
            if (!have_length_) { return fail(HttpError::NoLength); }
            if (content_length_ > body_cap_) { return fail(HttpError::TooBig); }
            phase_ = (content_length_ == 0) ? HttpPhase::Complete : HttpPhase::Body;
        }
        return phase_;
    }

    HttpPhase phase() const noexcept { return phase_; }
    HttpError error() const noexcept { return error_; }
    int status() const noexcept { return status_; }
    std::size_t content_length() const noexcept { return content_length_; }
    std::size_t body_bytes() const noexcept { return body_got_; }
    bool complete() const noexcept { return phase_ == HttpPhase::Complete; }

    // Valid only once `complete()`.
    std::string_view body() const noexcept { return std::string_view(body_, body_got_); }

    // A body that stopped short. The caller learns this from the SESSION ending
    // (a clean 0 from the socket) rather than from this class, because at this
    // layer a short body and a slow one are indistinguishable — which is
    // precisely why `Content-Length` is authoritative and a socket EOF is not.
    bool truncated() const noexcept {
        return phase_ == HttpPhase::Body && body_got_ < content_length_;
    }

private:
    HttpPhase fail(HttpError e) noexcept {
        error_ = e;
        phase_ = HttpPhase::Failed;
        return phase_;
    }

    // "HTTP/1.1 200 OK" - the number after the first space.
    bool parse_status(const char* s, std::size_t len) noexcept {
        if (len < 12) { return false; }
        if (!(s[0] == 'H' && s[1] == 'T' && s[2] == 'T' && s[3] == 'P' && s[4] == '/')) {
            return false;
        }
        std::size_t i = 5;
        while (i < len && s[i] != ' ') { ++i; }
        if (i >= len) { return false; }
        ++i;
        int n = 0;
        std::size_t digits = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            n = n * 10 + (s[i] - '0');
            ++i;
            ++digits;
        }
        if (digits != 3) { return false; }
        status_ = n;
        return true;
    }

    // Header names are case-insensitive and a venue may change the spelling on
    // any deploy, so this folds rather than comparing bytes.
    static bool name_is(const char* line, std::size_t len, const char* name) noexcept {
        std::size_t i = 0;
        for (; name[i] != '\0'; ++i) {
            if (i >= len) { return false; }
            char a = line[i];
            const char b = name[i];
            if (a >= 'A' && a <= 'Z') { a = static_cast<char>(a - 'A' + 'a'); }
            if (a != b) { return false; }
        }
        return true;
    }

    static std::size_t value_at(const char* line, std::size_t len, std::size_t after) noexcept {
        std::size_t i = after;
        while (i < len && (line[i] == ' ' || line[i] == '\t')) { ++i; }
        return i;
    }

    void note_header(const char* line, std::size_t len) noexcept {
        if (name_is(line, len, "content-length:")) {
            std::size_t i = value_at(line, len, 15);
            std::size_t n = 0;
            bool any = false;
            for (; i < len && line[i] >= '0' && line[i] <= '9'; ++i) {
                n = n * 10 + static_cast<std::size_t>(line[i] - '0');
                any = true;
            }
            if (any) {
                content_length_ = n;
                have_length_ = true;
            }
            return;
        }
        if (name_is(line, len, "transfer-encoding:")) {
            // Any transfer-encoding that mentions chunked. Not an exact match:
            // "chunked" may arrive as "gzip, chunked".
            for (std::size_t i = 18; i + 7 <= len; ++i) {
                if (name_is(line + i, len - i, "chunked")) {
                    chunked_ = true;
                    return;
                }
            }
        }
    }

    char* body_ = nullptr;
    std::size_t body_cap_ = 0;
    std::size_t body_got_ = 0;
    std::size_t content_length_ = 0;
    HttpPhase phase_ = HttpPhase::Status;
    HttpError error_ = HttpError::None;
    int status_ = 0;
    bool have_length_ = false;
    bool chunked_ = false;
    std::size_t line_len_ = 0;
    char line_[kHttpLineMax] = {};
};

const char* http_phase_name(HttpPhase p) noexcept;
const char* http_error_name(HttpError e) noexcept;

inline const char* http_phase_name(HttpPhase p) noexcept {
    switch (p) {
        case HttpPhase::Status:   return "status";
        case HttpPhase::Headers:  return "headers";
        case HttpPhase::Body:     return "body";
        case HttpPhase::Complete: return "complete";
        case HttpPhase::Failed:   return "failed";
    }
    return "?";
}

inline const char* http_error_name(HttpError e) noexcept {
    switch (e) {
        case HttpError::None:          return "none";
        case HttpError::BadStatusLine: return "bad-status-line";
        case HttpError::NotOk:         return "not-ok";
        case HttpError::Chunked:       return "chunked";
        case HttpError::NoLength:      return "no-length";
        case HttpError::TooBig:        return "too-big";
        case HttpError::HeaderTooLong: return "header-too-long";
        case HttpError::Overrun:       return "overrun";
    }
    return "?";
}

}  // namespace depthcharge::fw
