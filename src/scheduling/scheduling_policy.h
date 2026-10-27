#pragma once
#include "backend_pool.h"

class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;
    virtual BackendServer* selectBackend(BackendPool& pool) = 0;
};

