#include "round_robin_policy.h"

BackendServer* RoundRobinPolicy::selectBackend(BackendPool& pool) {
    if (pool.size() == 0) {
        return nullptr;
    }

    BackendServer& server = pool.getBackend(nextIndex_);
    nextIndex_ = (nextIndex_ + 1) % pool.size();
    return &server;
}
