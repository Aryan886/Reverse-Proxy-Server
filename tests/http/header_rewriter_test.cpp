#include <gtest/gtest.h>
#include "header_rewriter.h"
#include "http_parser.h"

TEST(HeaderRewriterTest, RemovesHopFieldsAndOverwritesUntrustedClientIdentity) {
    HttpRequest request;
    ASSERT_EQ(parseRequest("POST /api/echo?q=1 HTTP/1.1\r\nHost: demo.local\r\n"
        "Connection: keep-alive, X-Remove\r\nKeep-Alive: timeout=5\r\nX-Remove: secret\r\n"
        "X-Forwarded-For: fake\r\nX-Real-IP: fake\r\nForwarded: for=fake\r\n"
        "Via: 1.0 earlier\r\nContent-Length: 3\r\n\r\nabc", request).status, ParseStatus::Complete);
    auto message = serializeRequest(request, "127.0.0.1");
    EXPECT_EQ(message.find("secret"), std::string::npos);
    EXPECT_EQ(message.find("fake"), std::string::npos);
    EXPECT_EQ(message.find("Keep-Alive:"), std::string::npos);
    EXPECT_NE(message.find("X-Forwarded-For: 127.0.0.1\r\n"), std::string::npos);
    EXPECT_NE(message.find("Via: 1.0 earlier, 1.1 student-proxy\r\n"), std::string::npos);
    HttpRequest forwarded;
    EXPECT_EQ(parseRequest(message, forwarded).status, ParseStatus::Complete);
    EXPECT_EQ(forwarded.target, "/api/echo?q=1");
    EXPECT_EQ(forwarded.body, "abc");
}

TEST(HeaderRewriterTest, PreservesRepeatedCookiesAndRecalculatesLength) {
    HttpResponse response{200, "OK", {{"Connection", "X-Remove"}, {"X-Remove", "secret"},
        {"Content-Length", "999"}, {"Set-Cookie", "a=1"}, {"Set-Cookie", "b=2"}}, "hello"};
    auto message = serializeResponse(response);
    EXPECT_EQ(message.find("secret"), std::string::npos);
    EXPECT_EQ(message.find("999"), std::string::npos);
    HttpResponse forwarded;
    ASSERT_EQ(parseResponse(message, forwarded).status, ParseStatus::Complete);
    int cookies = 0;
    for (const auto& header : forwarded.headers) if (headerEquals(header.first, "Set-Cookie")) ++cookies;
    EXPECT_EQ(cookies, 2);
    EXPECT_EQ(forwarded.body, "hello");
}

TEST(HeaderRewriterTest, BodylessResponsesRetainCorrectFraming) {
    HttpResponse response{204, "No Content", {}, ""};
    EXPECT_EQ(serializeResponse(response).find("Content-Length"), std::string::npos);
    response = {304, "Not Modified", {{"Content-Length", "500"}}, ""};
    EXPECT_NE(serializeResponse(response).find("Content-Length: 500\r\n"), std::string::npos);
    response.version = "HTTP/1.0";
    EXPECT_NE(serializeResponse(response).find("Via: 1.0 student-proxy\r\n"), std::string::npos);
}

TEST(HeaderRewriterTest, ErrorResponsesHaveValidFraming) {
    for (int status : {400, 404, 408, 413, 417, 431, 501, 502, 503, 504}) {
        HttpResponse response;
        auto message = serializeResponse(makeErrorResponse(status));
        EXPECT_EQ(parseResponse(message, response).status, ParseStatus::Complete);
        EXPECT_EQ(response.statusCode, status);
        EXPECT_FALSE(response.body.empty());
        EXPECT_NE(message.find("Connection: close\r\n"), std::string::npos);
    }
}
