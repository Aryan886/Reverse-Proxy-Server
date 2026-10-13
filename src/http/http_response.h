#pragma once
#include "http_request.h"

struct HttpResponse {
    int statusCode = 200;
    std::string reason = "OK";
    HttpHeaders headers;
    std::string body;
    std::string version = "HTTP/1.1";
};
