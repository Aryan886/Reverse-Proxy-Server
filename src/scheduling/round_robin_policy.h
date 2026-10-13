#pragma once
#include "scheduling_policy.h"
#include <cstddef>

class RoundRobinPolicy : public SchedulingPolicy {
public:
    BackendServer* selectBackend(const std::vector<BackendServer*>& backends) override;
private:
    std::size_t nextIndex_ = 0;
};
