#pragma once

#include <vector>
#include <memory>
#include "backend_server.h"
using namespace std;

/*
Owns a collection of backendServer objects nd is allowed to own a 
BackendSever; everyone else only borrows non-owning raw ptrs
*/

class BackendPool{
    private:
        vector<unique_ptr<BackendServer>> backends_; //owning
        vector<BackendServer*> rawPointers_; //non-owning, kept in sync

    public:
        BackendPool() = default;

        BackendPool(const BackendPool&) = delete;
        BackendPool& operator=(const BackendPool&) = delete;

        void addBackend(unique_ptr<BackendServer> backend);
        bool removeBackend(BackendServer* target);


        // Returns the already mainatained raw pointer cache, not freshly built
        const vector<BackendServer*>& getBackends() const;

        size_t size() const;
};