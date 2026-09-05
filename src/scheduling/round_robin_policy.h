#pragma once
#include "scheduling_policy.h"
#include <cstddef>
using namespace std;

class RoundRobinPolicy : public SchedulingPolicy{
    public:
        BackendServer* selectBackend(const vector<BackendServer*>& backends) override;
    
    private:
        size_t nextIndex_ = 0; //size_t : type of integer suitable for representing size of something
};
