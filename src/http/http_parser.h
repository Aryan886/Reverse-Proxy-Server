#pragma once
#include "http_request.h"
#include "http_response.h"
#include <cstddef>
#include <string_view>

struct HttpLimits {
    std::size_t maxHeaderBytes = 16 * 1024;
    std::size_t maxRequestBodyBytes = 1024 * 1024;
    std::size_t maxResponseBodyBytes = 1024 * 1024;
};

enum class ParseStatus { NeedMoreData, Complete, Error };

struct ParseResult {
    ParseStatus status = ParseStatus::NeedMoreData;
    std::size_t bytesConsumed = 0;
    int errorStatus = 400;
    std::string error;
};

bool headerEquals(std::string_view first, std::string_view second);
std::string_view trimHeaderValue(std::string_view value);
ParseResult parseRequest(std::string_view buffer, HttpRequest& request,
                         const HttpLimits& limits = {}, bool eof = false);
ParseResult parseResponse(std::string_view buffer, HttpResponse& response,
                          const HttpLimits& limits = {}, bool eof = false);
