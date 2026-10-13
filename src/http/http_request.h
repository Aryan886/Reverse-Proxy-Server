#pragma once
#include <string>
#include <utility>
#include <vector>

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpRequest {
    std::string method;
    std::string target;
    HttpHeaders headers;
    std::string body;
};
