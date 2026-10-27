#pragma once
#include <string>

struct HttpRequest {
    std::string method;
    std::string target;
};
