#pragma once
#include "scheduling_policy.h"

class LeastConnectionPolicy : public SchedulingPolicy {
public:
    BackendServer* selectBackend(const std::vector<BackendServer*>& backends) override;
};
