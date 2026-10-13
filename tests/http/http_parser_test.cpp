#include <gtest/gtest.h>
#include "http_parser.h"

TEST(HttpParserTest, ReassemblesEverySplitOfPostIncludingBinaryBody) {
    std::string body("a\0b\r\nc", 6);
    std::string message = "POST /api/echo?mode=test HTTP/1.1\r\nHost: demo.local\r\nContent-Length: 6\r\n\r\n" + body;
    HttpRequest request;
    for (std::size_t split = 0; split < message.size(); ++split) {
        EXPECT_EQ(parseRequest(std::string_view(message).substr(0, split), request).status, ParseStatus::NeedMoreData) << split;
    }
    auto result = parseRequest(message, request);
    EXPECT_EQ(result.status, ParseStatus::Complete);
    EXPECT_EQ(result.bytesConsumed, message.size());
    EXPECT_EQ(request.body, body);
    EXPECT_EQ(request.target, "/api/echo?mode=test");
}

TEST(HttpParserTest, AcceptsCaseInsensitiveHeadersAndValidatedHosts) {
    for (const char* host : {"example.test:8080", "[::1]:8080", "[2001:db8::1]", "localhost"}) {
        HttpRequest request;
        EXPECT_EQ(parseRequest(std::string("GET /api HTTP/1.1\r\nhOsT: ") + host + "\r\n\r\n", request).status, ParseStatus::Complete);
    }
}

TEST(HttpParserTest, RejectsInvalidOrAmbiguousRequestHeaders) {
    for (const char* headers : {
        "Content-Length: 0\r\n",
        "Host: x\r\nHost: y\r\n",
        "Host: x\r\nContent-Length: 1\r\ncontent-length: 1\r\n",
        "Host: x\r\nContent-Length: -1\r\n",
        "Host: x\r\nContent-Length: 1,1\r\n",
        "Host: x\r\nContent-Length: 184467440737095516160\r\n",
        "Host: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n",
        "Host : x\r\n",
        "Host: x\r\n Folded: line\r\n",
        "Host: x\r\nBad: one\nother\r\n",
        "Host: x\r\nConnection: Host\r\n",
        "Host: x\r\nConnection: close,\r\n",
        "Host: x:bad\r\n",
        "Host: x:65536\r\n",
        "Host: a/b\r\n",
        "Host: [:::1]\r\n",
        "Host: user@x\r\n"
    }) {
        HttpRequest request;
        auto result = parseRequest(std::string("GET /api HTTP/1.1\r\n") + headers + "\r\n", request);
        EXPECT_EQ(result.status, ParseStatus::Error) << headers;
        EXPECT_EQ(result.errorStatus, 400) << headers;
    }
}

TEST(HttpParserTest, ReportsUnsupportedFeaturesBeforeForwarding) {
    HttpRequest request;
    auto transfer = parseRequest("GET /api HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n", request);
    EXPECT_EQ(transfer.errorStatus, 501);
    EXPECT_EQ(parseRequest("POST /api HTTP/1.1\r\nHost: x\r\nExpect: 100-continue\r\n\r\n", request).errorStatus, 417);
    EXPECT_EQ(parseRequest("CONNECT x:80 HTTP/1.1\r\nHost: x\r\n\r\n", request).errorStatus, 501);
    EXPECT_EQ(parseRequest("GET /api HTTP/1.1\r\nHost: x\r\nConnection: Upgrade\r\n\r\n", request).errorStatus, 501);
}

TEST(HttpParserTest, EnforcesLimitsAndPrematureEof) {
    HttpRequest request;
    HttpLimits limits;
    limits.maxHeaderBytes = 32;
    EXPECT_EQ(parseRequest(std::string(32, 'x'), request, limits).errorStatus, 431);
    limits.maxHeaderBytes = 1024;
    limits.maxRequestBodyBytes = 3;
    EXPECT_EQ(parseRequest("POST /api HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\n", request, limits).errorStatus, 413);
    EXPECT_EQ(parseRequest("POST /api HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\na", request, limits, true).status, ParseStatus::Error);
    EXPECT_EQ(parseRequest("GET /api HTTP/1.1\r\nHost: x", request, limits, true).status, ParseStatus::Error);
    EXPECT_EQ(parseRequest("POST /api HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabc", request, limits).status, ParseStatus::Complete);
}

TEST(HttpParserTest, RejectsInvalidTargetAndBufferedPipelining) {
    HttpRequest request;
    for (const char* target : {"http://example.test/api", "/api#fragment", "/api%xy", "/api%1", "/api\\id", "/api<id", "/api\"id"}) {
        EXPECT_EQ(parseRequest(std::string("GET ") + target + " HTTP/1.1\r\nHost: x\r\n\r\n", request).status, ParseStatus::Error);
    }
    std::string message = "GET /api HTTP/1.1\r\nHost: x\r\n\r\n";
    EXPECT_EQ(parseRequest(message + message, request).status, ParseStatus::Error);
}

TEST(HttpResponseParserTest, ParsesLengthAndPreservesRepeatedCookies) {
    HttpResponse response;
    std::string message = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nSet-Cookie: a=1\r\nSet-Cookie: b=2\r\n\r\nhello";
    for (std::size_t split = 0; split < message.size(); ++split) {
        EXPECT_EQ(parseResponse(std::string_view(message).substr(0, split), response).status, ParseStatus::NeedMoreData);
    }
    EXPECT_EQ(parseResponse(message, response).status, ParseStatus::Complete);
    EXPECT_EQ(response.body, "hello");
    EXPECT_EQ(response.headers.size(), 3u);
}

TEST(HttpResponseParserTest, CloseDelimitedRequiresEof) {
    HttpResponse response;
    std::string message = "HTTP/1.0 200 OK\r\n\r\nhello";
    EXPECT_EQ(parseResponse(message, response).status, ParseStatus::NeedMoreData);
    EXPECT_EQ(parseResponse(message, response, {}, true).status, ParseStatus::Complete);
    EXPECT_EQ(response.body, "hello");
    EXPECT_EQ(response.version, "HTTP/1.0");
}

TEST(HttpResponseParserTest, HandlesBodylessStatusesAndRepresentationLength) {
    HttpResponse response;
    EXPECT_EQ(parseResponse("HTTP/1.1 204 No Content\r\n\r\n", response).status, ParseStatus::Complete);
    EXPECT_EQ(parseResponse("HTTP/1.1 205 Reset Content\r\nContent-Length: 0\r\n\r\n", response).status, ParseStatus::Complete);
    EXPECT_EQ(parseResponse("HTTP/1.1 205 Reset Content\r\nContent-Length: 1\r\n\r\nx", response).errorStatus, 502);
    EXPECT_TRUE(response.body.empty());
    EXPECT_EQ(parseResponse("HTTP/1.1 304 Not Modified\r\nContent-Length: 9999999\r\n\r\n", response).status, ParseStatus::Complete);
    EXPECT_TRUE(response.body.empty());
    EXPECT_EQ(parseResponse("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n", response).errorStatus, 502);
}

TEST(HttpResponseParserTest, RejectsUnsupportedInvalidIncompleteAndOversizedResponses) {
    HttpResponse response;
    for (const char* message : {
        "HTTP/1.1 101 Switching Protocols\r\n\r\n",
        "HTTP/1.1 100 Continue\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nxx",
        "HTTP/1.1 304 Not Modified\r\n\r\nx"
    }) {
        auto result = parseResponse(message, response, {}, true);
        EXPECT_EQ(result.status, ParseStatus::Error) << message;
        EXPECT_EQ(result.errorStatus, 502);
    }
    HttpLimits limits;
    limits.maxResponseBodyBytes = 2;
    EXPECT_EQ(parseResponse("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n", response, limits).status, ParseStatus::Error);
    EXPECT_EQ(parseResponse("HTTP/1.1 200 OK\r\n\r\nabc", response, limits, true).status, ParseStatus::Error);
}
