#pragma once //prevents file's contents from being included twice in same translation unit (which causes redefinition error)
#include <vector>
using namespace  std;

class BackendServer; //forward declaration 

class SchedulingPolicy{
    public: 
        virtual ~SchedulingPolicy() = default;
        virtual BackendServer* selectBackend(const vector<BackendServer*>& backend) = 0;
};

