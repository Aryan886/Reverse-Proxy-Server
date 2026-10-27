#pragma once
#include <string>
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
    const Route* findRoute(const std::string& target) const;

private:
    // Pools and policies are created in main and must outlive the router.
    std::vector<Route> routes_;
};
