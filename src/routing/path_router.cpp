#include "path_router.h"
#include <stdexcept>

void PathRouter::addRoute(std::string prefix, BackendPool& pool, SchedulingPolicy& policy) {
    if (prefix.empty() || prefix.front() != '/' ||
        prefix.find_first_of("?# \t\r\n") != std::string::npos ||
        (prefix.size() > 1 && prefix.back() == '/')) {
        throw std::invalid_argument("Use a route such as /api, without a trailing slash");
    }
    routes_.push_back({prefix, &pool, &policy});
}

const Route* PathRouter::findRoute(const std::string& target) const {
    std::string path = target.substr(0, target.find('?'));
    // The first match wins. Put specific routes before a catch-all route.
    for (const Route& route : routes_) {
        if (path == route.prefix || route.prefix == "/" ||
            path.find(route.prefix + "/") == 0) {
            return &route;
        }
    }
    return nullptr;
}
