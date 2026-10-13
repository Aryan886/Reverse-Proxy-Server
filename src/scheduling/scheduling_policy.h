#pragma once
#include <vector>

class BackendServer;

class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;
    virtual BackendServer* selectBackend(const std::vector<BackendServer*>& backends) = 0;
};

