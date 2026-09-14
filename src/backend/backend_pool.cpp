#include "backend_pool.h"
#include <algorithm>
using namespace std;

void BackendPool::addBackend(unique_ptr<BackendServer> backend){
    //caches raw pointer before moving backend_ into backends_
    rawPointers_.push_back(backend.get());
    backends_.push_back(move(backend));
}

bool BackendPool::removeBackend(BackendServer* target){
    //finds the owning unique ptr who manages the obj matches target
    auto it = find_if(
        backends_.begin(), backends_.end(), 
        [target](const unique_ptr<BackendServer>& ptr){
            return ptr.get() == target;
        }
    );

    if(it==backends_.end()){
        return false;
    }

    auto rawIt = find(rawPointers_.begin(), rawPointers_.end(), target);
    if(rawIt != rawPointers_.end()){
        rawPointers_.erase(rawIt);
    }

    backends_.erase(it); // removes the unique ptr element, obj is destroyed
    return true;
}

//heands back a reference to maintained cache : O(1) TC as no contrustion and copy happens
const vector<BackendServer*>& BackendPool::getBackends() const{
    return rawPointers_;
}

//delegates to the vcetor's own .size()
size_t BackendPool::size() const{
    return backends_.size();
}

