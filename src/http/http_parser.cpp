#include "http_parser.h"
#include <limits>
#include <utility>

namespace {
ParseResult fail(int status, const std::string& message) {
    return {ParseStatus::Error, 0, status, message};
}

bool isToken(char character) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9')) return true;
    return std::string_view("!#$%&'*+-.^_`|~").find(character) != std::string_view::npos;
}

bool isHex(char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

bool parseNumber(std::string_view text, std::size_t& number) {
    if (text.empty()) return false;
    number = 0;
    for (char character : text) {
        if (character < '0' || character > '9') return false;
        std::size_t digit = static_cast<std::size_t>(character - '0');
        if (number > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
        number = number * 10 + digit;
    }
    return true;
}

bool validIpv6(std::string_view host) {
    auto compression = host.find("::");
    if (compression != std::string_view::npos && host.find("::", compression + 2) != std::string_view::npos) {
        return false;
    }
    int groups = 0;
    std::size_t start = 0;
    while (start < host.size()) {
        auto end = host.find(':', start);
        if (end == std::string_view::npos) end = host.size();
        auto group = host.substr(start, end - start);
        if (!group.empty()) {
            if (group.size() > 4) return false;
            for (char character : group) if (!isHex(character)) return false;
            ++groups;
        } else if (compression == std::string_view::npos ||
                   (start != compression && start != compression + 1)) {
            return false;
        }
        start = end + 1;
    }
    if (!host.empty() && host.back() == ':' && (host.size() < 2 || host[host.size() - 2] != ':')) return false;
    return compression == std::string_view::npos ? groups == 8 : groups < 8;
}

bool validHost(std::string_view value) {
    if (value.empty()) return false;
    std::string_view port;
    if (value.front() == '[') {
        auto end = value.find(']');
        if (end == std::string_view::npos || !validIpv6(value.substr(1, end - 1))) return false;
        if (end + 1 < value.size()) {
            if (value[end + 1] != ':') return false;
            port = value.substr(end + 2);
            if (port.empty()) return false;
        }
    } else {
        auto separator = value.find(':');
        auto host = value.substr(0, separator);
        if (host.empty()) return false;
        for (char character : host) {
            if (!((character >= 'a' && character <= 'z') ||
                  (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') ||
                  character == '.' || character == '-' || character == '_')) return false;
        }
        if (separator != std::string_view::npos) {
            port = value.substr(separator + 1);
            if (port.empty()) return false;
        }
    }
    if (!port.empty()) {
        std::size_t number = 0;
        if (!parseNumber(port, number) || number == 0 || number > 65535) return false;
    }
    return true;
}

ParseResult parseHead(std::string_view buffer, std::string& firstLine, HttpHeaders& headers,
                      const HttpLimits& limits, bool eof, bool response) {
    int status = response ? 502 : 400;
    auto end = buffer.find("\r\n\r\n");
    if (end == std::string_view::npos) {
        if (buffer.size() >= limits.maxHeaderBytes) return fail(response ? 502 : 431, "Header limit exceeded");
        if (eof) return fail(status, "EOF before complete headers");
        return {};
    }
    std::size_t bodyStart = end + 4;
    if (bodyStart > limits.maxHeaderBytes) return fail(response ? 502 : 431, "Header limit exceeded");
    auto lineEnd = buffer.find("\r\n");
    firstLine = std::string(buffer.substr(0, lineEnd));
    for (unsigned char character : firstLine) {
        if (character < 32 || character > 126) return fail(status, "Invalid start-line character");
    }
    std::size_t position = lineEnd + 2;
    while (position < end) {
        lineEnd = buffer.find("\r\n", position);
        auto line = buffer.substr(position, lineEnd - position);
        auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return fail(status, "Malformed header line");
        auto name = line.substr(0, colon);
        for (char character : name) if (!isToken(character)) return fail(status, "Invalid header name");
        auto value = trimHeaderValue(line.substr(colon + 1));
        for (unsigned char character : value) {
            if ((character < 32 && character != '\t') || character == 127) return fail(status, "Invalid header value");
        }
        headers.emplace_back(name, value);
        position = lineEnd + 2;
    }
    return {ParseStatus::Complete, bodyStart, 0, {}};
}

struct Framing {
    bool hasLength = false;
    std::size_t length = 0;
};

ParseResult validateHeaders(const HttpHeaders& headers, Framing& framing, bool response) {
    int failureStatus = response ? 502 : 400;
    int hostCount = 0;
    bool transferEncoding = false;
    bool expect = false;
    bool upgrade = false;
    for (const auto& header : headers) {
        if (headerEquals(header.first, "Host")) {
            ++hostCount;
            if (!response && !validHost(header.second)) return fail(400, "Invalid Host");
        } else if (headerEquals(header.first, "Content-Length")) {
            if (framing.hasLength || !parseNumber(header.second, framing.length)) {
                return fail(failureStatus, "Invalid or repeated Content-Length");
            }
            framing.hasLength = true;
        } else if (headerEquals(header.first, "Transfer-Encoding")) {
            transferEncoding = true;
        } else if (headerEquals(header.first, "Expect")) {
            expect = true;
        } else if (headerEquals(header.first, "Upgrade")) {
            upgrade = true;
        } else if (headerEquals(header.first, "Connection")) {
            std::string_view value = header.second;
            while (true) {
                auto comma = value.find(',');
                auto token = trimHeaderValue(value.substr(0, comma));
                if (token.empty()) return fail(failureStatus, "Empty Connection option");
                for (char character : token) if (!isToken(character)) return fail(failureStatus, "Invalid Connection option");
                if (headerEquals(token, "Host") || headerEquals(token, "Content-Length") ||
                    headerEquals(token, "Transfer-Encoding")) return fail(failureStatus, "Connection names a framing or routing field");
                if (headerEquals(token, "upgrade")) upgrade = true;
                if (comma == std::string_view::npos) break;
                value.remove_prefix(comma + 1);
            }
        }
    }
    if (!response && hostCount != 1) return fail(400, "Exactly one Host header is required");
    if (transferEncoding && framing.hasLength) return fail(failureStatus, "Conflicting message framing");
    if (transferEncoding) return fail(response ? 502 : 501, "Transfer-Encoding is unsupported");
    if (upgrade) return fail(response ? 502 : 501, "Protocol upgrades are unsupported");
    if (!response && expect) return fail(417, "Expect is unsupported");
    return {ParseStatus::Complete, 0, 0, {}};
}

ParseResult readBody(std::string_view buffer, std::size_t bodyStart, std::size_t length,
                     std::size_t limit, bool eof, bool response, std::string& body) {
    if (length > limit) return fail(response ? 502 : 413, "Body limit exceeded");
    std::size_t available = buffer.size() - bodyStart;
    if (available < length) {
        if (eof) return fail(response ? 502 : 400, "EOF before complete body");
        return {};
    }
    if (available > length) return fail(response ? 502 : 400, "Extra bytes after one message; pipelining is unsupported");
    body = std::string(buffer.substr(bodyStart, length));
    return {ParseStatus::Complete, bodyStart + length, 0, {}};
}
}

bool headerEquals(std::string_view first, std::string_view second) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
        char left = first[index], right = second[index];
        if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
        if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
        if (left != right) return false;
    }
    return true;
}

std::string_view trimHeaderValue(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return value;
}

ParseResult parseRequest(std::string_view buffer, HttpRequest& request, const HttpLimits& limits, bool eof) {
    HttpRequest parsed;
    std::string line;
    auto result = parseHead(buffer, line, parsed.headers, limits, eof, false);
    if (result.status != ParseStatus::Complete) return result;
    std::size_t bodyStart = result.bytesConsumed;
    auto firstSpace = line.find(' ');
    auto secondSpace = firstSpace == std::string::npos ? std::string::npos : line.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos ||
        line.find(' ', secondSpace + 1) != std::string::npos || line.substr(secondSpace + 1) != "HTTP/1.1") {
        return fail(400, "Expected METHOD origin-target HTTP/1.1");
    }
    parsed.method = line.substr(0, firstSpace);
    if (parsed.method.empty()) return fail(400, "Missing method");
    for (char character : parsed.method) if (!isToken(character)) return fail(400, "Invalid method");
    if (parsed.method != "GET" && parsed.method != "POST") return fail(501, "Only GET and POST are supported");
    parsed.target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    if (parsed.target.empty() || parsed.target.front() != '/') return fail(400, "Only origin-form targets are supported");
    for (std::size_t index = 0; index < parsed.target.size(); ++index) {
        unsigned char character = parsed.target[index];
        bool letterOrDigit = (character >= 'a' && character <= 'z') ||
                             (character >= 'A' && character <= 'Z') ||
                             (character >= '0' && character <= '9');
        if (!letterOrDigit && std::string_view("/?:@!$&'()*+,;=-._~%").find(character) == std::string_view::npos) {
            return fail(400, "Invalid request target");
        }
        if (character == '%' && (index + 2 >= parsed.target.size() ||
            !isHex(parsed.target[index + 1]) || !isHex(parsed.target[index + 2]))) return fail(400, "Invalid percent escape");
    }
    Framing framing;
    result = validateHeaders(parsed.headers, framing, false);
    if (result.status == ParseStatus::Error) return result;
    result = readBody(buffer, bodyStart, framing.length, limits.maxRequestBodyBytes, eof, false, parsed.body);
    if (result.status == ParseStatus::Complete) request = std::move(parsed);
    return result;
}

ParseResult parseResponse(std::string_view buffer, HttpResponse& response, const HttpLimits& limits, bool eof) {
    HttpResponse parsed;
    std::string line;
    auto result = parseHead(buffer, line, parsed.headers, limits, eof, true);
    if (result.status != ParseStatus::Complete) return result;
    std::size_t bodyStart = result.bytesConsumed;
    if (line.size() < 13 || (line.substr(0, 9) != "HTTP/1.1 " && line.substr(0, 9) != "HTTP/1.0 ") || line[12] != ' ') {
        return fail(502, "Malformed upstream status line");
    }
    std::size_t status = 0;
    if (!parseNumber(std::string_view(line).substr(9, 3), status) || status < 200 || status > 599) {
        return fail(502, "Informational or invalid upstream status is unsupported");
    }
    parsed.statusCode = static_cast<int>(status);
    parsed.version = line.substr(0, 8);
    parsed.reason = line.substr(13);
    Framing framing;
    result = validateHeaders(parsed.headers, framing, true);
    if (result.status == ParseStatus::Error) return result;
    bool bodyless = status == 204 || status == 205 || status == 304;
    if (status == 204 && framing.hasLength) return fail(502, "204 must not carry Content-Length");
    if (status == 205 && framing.length != 0) return fail(502, "205 must not carry a body");
    if (bodyless) {
        result = readBody(buffer, bodyStart, 0, limits.maxResponseBodyBytes, eof, true, parsed.body);
    } else if (framing.hasLength) {
        result = readBody(buffer, bodyStart, framing.length, limits.maxResponseBodyBytes, eof, true, parsed.body);
    } else {
        if (buffer.size() - bodyStart > limits.maxResponseBodyBytes) return fail(502, "Body limit exceeded");
        if (!eof) return {};
        parsed.body = std::string(buffer.substr(bodyStart));
        result = {ParseStatus::Complete, buffer.size(), 0, {}};
    }
    if (result.status == ParseStatus::Complete) response = std::move(parsed);
    return result;
}
