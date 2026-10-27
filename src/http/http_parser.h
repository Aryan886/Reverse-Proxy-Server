#pragma once
#include "http_request.h"

HttpRequest parseRequest(const std::string& message);
std::string makeBackendRequest(const HttpRequest& request, const std::string& host, int port);
std::string makeErrorResponse(int status, const std::string& reason);
