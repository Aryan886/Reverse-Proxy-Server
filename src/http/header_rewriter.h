#pragma once
#include "http_request.h"
#include "http_response.h"

std::string serializeRequest(const HttpRequest& request, const std::string& clientAddress);
std::string serializeResponse(const HttpResponse& response);
HttpResponse makeErrorResponse(int status);
