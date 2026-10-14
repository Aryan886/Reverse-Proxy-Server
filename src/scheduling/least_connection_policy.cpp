#include "least_connection_policy.h"
#include "backend_server.h"

BackendServer* LeastConnectionPolicy :: selectBackend(const vector<BackendServer*>& backends){
    if(backends.empty()){
        return nullptr;
    }

    BackendServer* best = backends[0];
    for(BackendServer* candidate : backends){
        if(candidate->getActiveConnections() < best->getActiveConnections()){
            best = candidate;
        }

    }
    return best;
}