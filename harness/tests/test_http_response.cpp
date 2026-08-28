// test_http_response.cpp — the HTTP reader, fed the way a socket feeds it.
//
// M5 stage D-A2. The point of every case here is that `feed()` must give the
// same answer regardless of how the bytes are cut up, because a non-blocking
// socket cuts them arbitrarily. The M3 transport's `upgrade headers did not end
// in 512 bytes` defect was a header reader that assumed one read; this is the
// test suite that defect earned.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "http_response.hpp"

using depthcharge::fw::HttpError;
using depthcharge::fw::HttpPhase;
using depthcharge::fw::HttpResponse;

namespace {

std::string ok_response(const std::string& body) {
    return "HTTP/1.1 200 OK\r\n"
           "Server: nginx\r\n"
           "Content-Type: application/json;charset=UTF-8\r\n"
           "Content-Length: " +
           std::to_string(body.size()) +
           "\r\n"
           "Connection: close\r\n"
           "\r\n" +
           body;
}

// Feed `text` in chunks of exactly `n` bytes (the last one short).
HttpPhase feed_in_chunks(HttpResponse& r, const std::string& text, std::size_t n) {
    HttpPhase p = HttpPhase::Status;
    for (std::size_t i = 0; i < text.size(); i += n) {
        const std::size_t take = (i + n <= text.size()) ? n : text.size() - i;
        p = r.feed(text.data() + i, take);
        if (p == HttpPhase::Failed) { return p; }
    }
    return p;
}

}  // namespace

TEST_CASE("a well-formed response is read whole") {
    const std::string body = R"({"lastUpdateId":42,"bids":[],"asks":[]})";
    const std::string text = ok_response(body);

    std::vector<char> buf(4096);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(r.feed(text.data(), text.size()) == HttpPhase::Complete);
    CHECK(r.status() == 200);
    CHECK(r.content_length() == body.size());
    CHECK(std::string(r.body()) == body);
    CHECK(r.error() == HttpError::None);
}

// THE CASE THIS FILE EXISTS FOR. Every chunk size from 1 byte upward must give
// byte-identical output, because the socket chooses the cut and we do not.
TEST_CASE("the answer does not depend on how the socket cuts the bytes") {
    const std::string body = R"({"lastUpdateId":7,"bids":[["1.00","2.00"]],"asks":[]})";
    const std::string text = ok_response(body);

    for (std::size_t chunk = 1; chunk <= text.size(); ++chunk) {
        std::vector<char> buf(4096);
        HttpResponse r;
        r.begin(buf.data(), buf.size());
        const HttpPhase p = feed_in_chunks(r, text, chunk);
        CAPTURE(chunk);
        REQUIRE(p == HttpPhase::Complete);
        CHECK(r.status() == 200);
        CHECK(std::string(r.body()) == body);
    }
}

TEST_CASE("header names are matched case-insensitively") {
    const std::string body = "{}";
    std::string text =
        "HTTP/1.1 200 OK\r\nCONTENT-LENGTH: 2\r\nConnection: close\r\n\r\n" + body;
    std::vector<char> buf(64);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(r.feed(text.data(), text.size()) == HttpPhase::Complete);
    CHECK(std::string(r.body()) == body);
}

// REFUSED, NEVER MIS-PARSED. A chunked body read as flat hands the JSON parser
// hex length prefixes embedded mid-document.
TEST_CASE("chunked transfer-encoding is refused rather than mis-parsed") {
    std::string text =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
        "2\r\n{}\r\n0\r\n\r\n";
    std::vector<char> buf(64);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(r.feed(text.data(), text.size()) == HttpPhase::Failed);
    CHECK(r.error() == HttpError::Chunked);

    SUBCASE("even when it arrives alongside another encoding") {
        std::string t2 =
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n";
        std::vector<char> b2(64);
        HttpResponse r2;
        r2.begin(b2.data(), b2.size());
        CHECK(r2.feed(t2.data(), t2.size()) == HttpPhase::Failed);
        CHECK(r2.error() == HttpError::Chunked);
    }
}

TEST_CASE("a body larger than the buffer is refused, not truncated") {
    // A truncated JSON body is not a smaller book, it is an unparseable one —
    // and if it did parse it would be a book missing its deepest levels with
    // nothing saying so.
    std::string text = "HTTP/1.1 200 OK\r\nContent-Length: 5000\r\n\r\n";
    std::vector<char> buf(256);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(r.feed(text.data(), text.size()) == HttpPhase::Failed);
    CHECK(r.error() == HttpError::TooBig);
    CHECK(r.content_length() == 5000);   // reported, so the log can name it
}

TEST_CASE("a response with no Content-Length is refused") {
    std::string text = "HTTP/1.1 200 OK\r\nServer: nginx\r\n\r\n{}";
    std::vector<char> buf(64);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(r.feed(text.data(), text.size()) == HttpPhase::Failed);
    CHECK(r.error() == HttpError::NoLength);
}

// THE TWO STATUSES THAT ARE NOT TRANSPORT FAILURES. 429 is a rate limit and 418
// is the venue's answer to a client that ignored one; both mean the SCHEDULE is
// wrong, and the status must survive to the caller so the schedule can act.
TEST_CASE("a rate-limit status is reported with its code intact") {
    for (const int code : {429, 418, 500}) {
        std::string text = "HTTP/1.1 " + std::to_string(code) +
                           " x\r\nContent-Length: 0\r\n\r\n";
        std::vector<char> buf(64);
        HttpResponse r;
        r.begin(buf.data(), buf.size());
        CAPTURE(code);
        CHECK(r.feed(text.data(), text.size()) == HttpPhase::Failed);
        CHECK(r.error() == HttpError::NotOk);
        CHECK(r.status() == code);
    }
}

TEST_CASE("a malformed status line fails rather than defaulting to 200") {
    for (const char* bad : {"NOT-HTTP\r\n\r\n", "HTTP/1.1 \r\n\r\n", "HTTP/1.1 2O0 OK\r\n\r\n"}) {
        std::string text = bad;
        std::vector<char> buf(64);
        HttpResponse r;
        r.begin(buf.data(), buf.size());
        CAPTURE(bad);
        CHECK(r.feed(text.data(), text.size()) == HttpPhase::Failed);
        CHECK(r.error() == HttpError::BadStatusLine);
        CHECK(r.status() != 200);
    }
}

TEST_CASE("a short body is visible as truncated rather than as complete") {
    const std::string body = "0123456789";
    std::string text = ok_response(body);
    text.resize(text.size() - 4);          // the socket died mid-body

    std::vector<char> buf(256);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(r.feed(text.data(), text.size()) == HttpPhase::Body);
    CHECK_FALSE(r.complete());
    CHECK(r.truncated());
    CHECK(r.body_bytes() == body.size() - 4);
}

TEST_CASE("a server sending more than it declared is an overrun, not extra data") {
    const std::string body = "{}";
    std::string text = ok_response(body) + "surplus";
    std::vector<char> buf(256);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(r.feed(text.data(), text.size()) == HttpPhase::Failed);
    CHECK(r.error() == HttpError::Overrun);
}

TEST_CASE("an over-long header line fails loudly") {
    std::string text = "HTTP/1.1 200 OK\r\nX-Silly: " +
                       std::string(depthcharge::fw::kHttpLineMax + 16, 'a') + "\r\n\r\n";
    std::vector<char> buf(64);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(r.feed(text.data(), text.size()) == HttpPhase::Failed);
    CHECK(r.error() == HttpError::HeaderTooLong);
}

TEST_CASE("a 64 KB body — the real seed size — reads back byte-exact") {
    // The committed BTCUSDT bodies are 64,046 B. This is the size that matters,
    // and the one where an off-by-one in the bulk copy would show.
    std::string body(64'046, 'x');
    body[0] = '{';
    body[body.size() - 1] = '}';
    const std::string text = ok_response(body);

    std::vector<char> buf(96 * 1024);
    HttpResponse r;
    r.begin(buf.data(), buf.size());
    CHECK(feed_in_chunks(r, text, 1436) == HttpPhase::Complete);   // one MSS at a time
    CHECK(r.body_bytes() == body.size());
    CHECK(std::string(r.body()) == body);
}
