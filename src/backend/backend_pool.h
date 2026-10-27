#pragma once
#include <cstddef>
#include <vector>
#include "backend_server.h"

class BackendPool {
public:
    void addBackend(const BackendServer& backend);
    BackendServer& getBackend(std::size_t index);
    std::size_t size() const;

private:
    std::vector<BackendServer> backends_;
};
