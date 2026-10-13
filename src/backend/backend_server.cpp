#include "backend_server.h"
#include <limits>
#include <stdexcept>
#include <utility>

BackendServer::BackendServer(std::string host, int port)
    : host_(std::move(host)), port_(port) {
    if (host_.empty()) throw std::invalid_argument("Backend host must not be empty");
    if (port_ < 1 || port_ > 65535) {
        throw std::invalid_argument("Backend port must be between 1 and 65535");
    }
}

const std::string& BackendServer::getHost() const { return host_; }
int BackendServer::getPort() const { return port_; }
int BackendServer::getActiveConnections() const { return activeConnections_; }

void BackendServer::incrementConnections() {
    if (activeConnections_ == std::numeric_limits<int>::max()) {
        throw std::overflow_error("Backend connection count overflow");
    }
    ++activeConnections_;
}

void BackendServer::decrementConnections() {
    if (activeConnections_ > 0) --activeConnections_;
}

bool BackendServer::isHealthy() const { return isHealthy_; }
void BackendServer::setHealthy(bool healthy) { isHealthy_ = healthy; }
