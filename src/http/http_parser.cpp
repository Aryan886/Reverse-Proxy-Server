#include "http_parser.h"
#include <sstream>
#include <stdexcept>

HttpRequest parseRequest(const std::string& message) {
    if (message.find("\r\n\r\n") == std::string::npos) {
        throw std::invalid_argument("Incomplete request headers");
    }

    HttpRequest request;
    std::string version;
    std::string extra;
    std::istringstream firstLine(message.substr(0, message.find("\r\n")));
    if (!(firstLine >> request.method >> request.target >> version) || (firstLine >> extra)) {
        throw std::invalid_argument("Expected METHOD /path HTTP/1.1");
    }
    if (version != "HTTP/1.0" && version != "HTTP/1.1") {
        throw std::invalid_argument("Only HTTP/1.0 and HTTP/1.1 are supported");
    }
    if (request.target.front() != '/') {
        throw std::invalid_argument("The request path must start with /");
    }
    for (unsigned char character : request.target) {
        if (character <= 32 || character == 127) {
            throw std::invalid_argument("Invalid character in request path");
        }
    }
    return request;
}

std::string makeBackendRequest(const HttpRequest& request, const std::string& host, int port) {
    // The demo sends a fresh GET instead of copying browser headers and body.
    return "GET " + request.target + " HTTP/1.0\r\nHost: " + host + ":" +
           std::to_string(port) + "\r\nConnection: close\r\n\r\n";
}

std::string makeErrorResponse(int status, const std::string& reason) {
    std::string body = std::to_string(status) + " " + reason + "\n";
    std::string response = "HTTP/1.0 " + std::to_string(status) + " " + reason + "\r\n";
    if (status == 405) {
        response += "Allow: GET\r\n";
    }
    return response + "Content-Type: text/plain\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}
