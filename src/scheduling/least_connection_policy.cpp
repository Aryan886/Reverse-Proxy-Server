#include "least_connection_policy.h"
#include "backend_server.h"

BackendServer* LeastConnectionPolicy::selectBackend(const std::vector<BackendServer*>& backends) {
    BackendServer* best = nullptr;
    for (BackendServer* candidate : backends) {
        if (!candidate || !candidate->isHealthy()) continue;
        if (!best || candidate->getActiveConnections() < best->getActiveConnections()) {
            best = candidate;
        }
    }
    return best;
}
