#include "least_connection_policy.h"

BackendServer* LeastConnectionPolicy::selectBackend(BackendPool& pool) {
    BackendServer* selectedServer = nullptr;
    for (std::size_t index = 0; index < pool.size(); ++index) {
        BackendServer& server = pool.getBackend(index);
        if (selectedServer == nullptr ||
            server.getActiveConnections() < selectedServer->getActiveConnections()) {
            selectedServer = &server;
        }
    }
    return selectedServer;
}
