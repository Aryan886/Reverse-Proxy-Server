#pragma once
#include <string>
#include <string_view>
#include <vector>

class BackendPool;
class SchedulingPolicy;

struct Route {
    std::string prefix;
    BackendPool* pool;
    SchedulingPolicy* policy;
};

class PathRouter {
public:
    void addRoute(std::string prefix, BackendPool& pool, SchedulingPolicy& policy);
    const Route* findRoute(std::string_view target) const;
private:
    std::vector<Route> routes_;
};
