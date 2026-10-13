#include "path_router.h"
#include <stdexcept>
#include <utility>

void PathRouter::addRoute(std::string prefix, BackendPool& pool, SchedulingPolicy& policy) {
    if (prefix.empty() || prefix.front() != '/' ||
        prefix.find_first_of("?# \t\r\n") != std::string::npos ||
        (prefix.size() > 1 && prefix.back() == '/')) {
        throw std::invalid_argument("Route prefix must start with / and have no query, spaces or trailing slash");
    }
    for (const auto& route : routes_) {
        if (route.prefix == prefix) throw std::invalid_argument("Duplicate route prefix: " + prefix);
    }
    routes_.push_back({std::move(prefix), &pool, &policy});
}

const Route* PathRouter::findRoute(std::string_view target) const {
    auto path = target.substr(0, target.find('?'));
    const Route* best = nullptr;
    for (const auto& route : routes_) {
        if (path.size() < route.prefix.size() || path.substr(0, route.prefix.size()) != route.prefix) continue;
        bool boundary = route.prefix == "/" || path.size() == route.prefix.size() || path[route.prefix.size()] == '/';
        if (boundary && (!best || route.prefix.size() > best->prefix.size())) best = &route;
    }
    return best;
}
