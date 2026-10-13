#include "round_robin_policy.h"
#include "backend_server.h"

BackendServer* RoundRobinPolicy::selectBackend(const std::vector<BackendServer*>& backends) {
    if (backends.empty()) return nullptr;
    nextIndex_ %= backends.size();
    for (std::size_t attempt = 0; attempt < backends.size(); ++attempt) {
        BackendServer* backend = backends[nextIndex_];
        nextIndex_ = (nextIndex_ + 1) % backends.size();
        if (backend && backend->isHealthy()) return backend;
    }
    return nullptr;
}
