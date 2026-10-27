#include "backend_pool.h"

void BackendPool::addBackend(const BackendServer& backend) {
    backends_.push_back(backend);
}

BackendServer& BackendPool::getBackend(std::size_t index) {
    return backends_.at(index);
}

std::size_t BackendPool::size() const {
    return backends_.size();
}

