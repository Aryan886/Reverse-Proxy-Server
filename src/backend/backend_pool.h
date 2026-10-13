#pragma once
#include <cstddef>
#include <memory>
#include <vector>
#include "backend_server.h"

class BackendPool {
public:
    BackendPool() = default;
    BackendPool(const BackendPool&) = delete;
    BackendPool& operator=(const BackendPool&) = delete;

    void addBackend(std::unique_ptr<BackendServer> backend);
    bool removeBackend(BackendServer* target);
    const std::vector<BackendServer*>& getBackends() const;
    std::size_t size() const;

private:
    std::vector<std::unique_ptr<BackendServer>> backends_;
    std::vector<BackendServer*> rawPointers_; // Borrowed; owners above keep addresses stable.
};
