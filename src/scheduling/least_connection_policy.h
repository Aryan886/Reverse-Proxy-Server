#pragma once
#include "scheduling_policy.h"

class LeastConnectionPolicy : public SchedulingPolicy {
public:
    BackendServer* selectBackend(BackendPool& pool) override;
};
