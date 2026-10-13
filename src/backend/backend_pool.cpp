#include "backend_pool.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

void BackendPool::addBackend(std::unique_ptr<BackendServer> backend) {
    if (!backend) throw std::invalid_argument("Cannot add a null backend");
    // Finish potentially throwing allocations before changing either membership list.
    if (backends_.size() == backends_.capacity()) backends_.reserve(backends_.size() * 2 + 1);
    if (rawPointers_.size() == rawPointers_.capacity()) rawPointers_.reserve(rawPointers_.size() * 2 + 1);
    rawPointers_.push_back(backend.get());
    backends_.push_back(std::move(backend));
}

bool BackendPool::removeBackend(BackendServer* target) {
    auto owner = std::find_if(backends_.begin(), backends_.end(),
        [target](const std::unique_ptr<BackendServer>& backend) { return backend.get() == target; });
    if (owner == backends_.end()) return false;
    rawPointers_.erase(std::find(rawPointers_.begin(), rawPointers_.end(), target));
    backends_.erase(owner);
    return true;
}

const std::vector<BackendServer*>& BackendPool::getBackends() const { return rawPointers_; }
std::size_t BackendPool::size() const { return backends_.size(); }

