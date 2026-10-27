#include <gtest/gtest.h>
#include "http_parser.h"
#include <stdexcept>

TEST(HttpParserTest, ReadsMethodAndTarget) {
    HttpRequest request = parseRequest("GET /api/id?name=test HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_EQ(request.method, "GET");
    EXPECT_EQ(request.target, "/api/id?name=test");
}

TEST(HttpParserTest, RejectsIncompleteOrInvalidRequestLines) {
    EXPECT_THROW(parseRequest("GET /api HTTP/1.1\r\n"), std::invalid_argument);
    EXPECT_THROW(parseRequest("GET\r\n\r\n"), std::invalid_argument);
    EXPECT_THROW(parseRequest("GET api HTTP/1.1\r\n\r\n"), std::invalid_argument);
    EXPECT_THROW(parseRequest("GET /api HTTP/2.0\r\n\r\n"), std::invalid_argument);
    EXPECT_THROW(parseRequest("GET /api HTTP/1.1 extra\r\n\r\n"), std::invalid_argument);
}

TEST(HttpParserTest, BuildsOnlyTheSmallBackendRequest) {
    HttpRequest request = parseRequest(
        "GET /api/id?name=test HTTP/1.1\r\nHost: browser\r\nCookie: demo=1\r\n\r\n");
    EXPECT_EQ(makeBackendRequest(request, "127.0.0.1", 9001),
              "GET /api/id?name=test HTTP/1.0\r\nHost: 127.0.0.1:9001\r\nConnection: close\r\n\r\n");
}

TEST(HttpParserTest, ErrorResponseIncludesItsLengthAndAllowedMethod) {
    std::string response = makeErrorResponse(405, "Method Not Allowed");
    std::string body = "405 Method Not Allowed\n";
    EXPECT_EQ(response.find("HTTP/1.0 405 Method Not Allowed\r\n"), 0u);
    EXPECT_NE(response.find("Allow: GET\r\n"), std::string::npos);
    EXPECT_NE(response.find("Content-Length: " + std::to_string(body.size())), std::string::npos);
    EXPECT_EQ(response.substr(response.find("\r\n\r\n") + 4), body);
}
