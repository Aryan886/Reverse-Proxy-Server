#include "backend_server.h"
#include <stdexcept>

BackendServer::BackendServer(std::string host, int port)
    : host_(host), port_(port) {
    if (host_.empty()) {
        throw std::invalid_argument("Backend host must not be empty");
    }
    if (port_ < 1 || port_ > 65535) {
        throw std::invalid_argument("Backend port must be between 1 and 65535");
    }
}

const std::string& BackendServer::getHost() const { return host_; }
int BackendServer::getPort() const { return port_; }
int BackendServer::getActiveConnections() const { return activeConnections_; }

void BackendServer::incrementConnections() {
    ++activeConnections_;
}

void BackendServer::decrementConnections() {
    if (activeConnections_ > 0) {
        --activeConnections_;
    }
}
