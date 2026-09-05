#include "round_robin_policy.h"
#include "backend_server.h"

using namespace std;

BackendServer* RoundRobinPolicy::selectBackend(const vector<BackendServer*>& backends){
    if(backends.empty()){
        return nullptr
    }

    BackendServer* chosen = backends[nextIndex_ % backends.size()];
    ++nextIndex_;
    return chosen;
}