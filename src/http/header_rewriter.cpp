#include "header_rewriter.h"
#include "http_parser.h"

namespace {
bool removeHeader(const std::string& name, const HttpHeaders& headers) {
    for (const char* hop : {"Connection", "Keep-Alive", "Proxy-Connection", "Proxy-Authenticate",
                            "Proxy-Authorization", "TE", "Trailer", "Transfer-Encoding", "Upgrade", "Content-Length"}) {
        if (headerEquals(name, hop)) return true;
    }
    for (const auto& header : headers) {
        if (!headerEquals(header.first, "Connection")) continue;
        std::string_view value = header.second;
        while (true) {
            auto comma = value.find(',');
            if (headerEquals(name, trimHeaderValue(value.substr(0, comma)))) return true;
            if (comma == std::string_view::npos) break;
            value.remove_prefix(comma + 1);
        }
    }
    return false;
}

std::string rewriteHeaders(const HttpHeaders& headers, const std::string* clientAddress,
                           std::string_view receivedProtocol) {
    std::string output;
    std::string via;
    for (const auto& header : headers) {
        if (removeHeader(header.first, headers)) continue;
        if (clientAddress && (headerEquals(header.first, "X-Forwarded-For") ||
            headerEquals(header.first, "X-Real-IP") || headerEquals(header.first, "Forwarded"))) continue;
        if (headerEquals(header.first, "Via")) {
            if (!via.empty()) via += ", ";
            via += header.second;
        } else {
            output += header.first + ": " + header.second + "\r\n";
        }
    }
    if (clientAddress) {
        output += "X-Forwarded-For: " + *clientAddress + "\r\nX-Real-IP: " + *clientAddress + "\r\n";
    }
    if (!via.empty()) via += ", ";
    output += "Via: " + via + std::string(receivedProtocol) + " student-proxy\r\nConnection: close\r\n";
    return output;
}
}

std::string serializeRequest(const HttpRequest& request, const std::string& clientAddress) {
    return request.method + " " + request.target + " HTTP/1.1\r\n" +
           rewriteHeaders(request.headers, &clientAddress, "1.1") +
           "Content-Length: " + std::to_string(request.body.size()) + "\r\n\r\n" + request.body;
}

std::string serializeResponse(const HttpResponse& response) {
    std::string output = "HTTP/1.1 " + std::to_string(response.statusCode) + " " + response.reason + "\r\n" +
                         rewriteHeaders(response.headers, nullptr, std::string_view(response.version).substr(5));
    if (response.statusCode == 304) {
        // A 304 length describes the selected representation, not this body.
        for (const auto& header : response.headers) {
            if (headerEquals(header.first, "Content-Length")) output += "Content-Length: " + header.second + "\r\n";
        }
    } else if (response.statusCode != 204) {
        output += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    }
    output += "\r\n";
    if (response.statusCode != 204 && response.statusCode != 304) output += response.body;
    return output;
}

HttpResponse makeErrorResponse(int status) {
    HttpResponse response;
    response.statusCode = status;
    switch (status) {
        case 400: response.reason = "Bad Request"; break;
        case 404: response.reason = "Not Found"; break;
        case 408: response.reason = "Request Timeout"; break;
        case 413: response.reason = "Content Too Large"; break;
        case 417: response.reason = "Expectation Failed"; break;
        case 431: response.reason = "Request Header Fields Too Large"; break;
        case 501: response.reason = "Not Implemented"; break;
        case 502: response.reason = "Bad Gateway"; break;
        case 503: response.reason = "Service Unavailable"; break;
        case 504: response.reason = "Gateway Timeout"; break;
        default: response.reason = "Internal Server Error"; response.statusCode = 500; break;
    }
    response.body = std::to_string(response.statusCode) + " " + response.reason + "\n";
    response.headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
    return response;
}
