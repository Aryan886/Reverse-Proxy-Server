#pragma once
#include <string>
using namespace std;

class BackendServer{
    public:
        BackendServer(string host, int port);
        const string& getHost() const;
        int getPort() const;

        int getActiveConnections() const;
        void incrementConnections();
        void decremenetConnections();

        bool isHealthy() const;
        void setHealthy(bool healthy);

    private:
        string host_;
        int port_;
        int activeConnections_ = 0;
        bool isHealthy_ = true;
};