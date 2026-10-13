#pragma once
#include <string>

class BackendServer {
public:
    BackendServer(std::string host, int port);
    const std::string& getHost() const;
    int getPort() const;
    int getActiveConnections() const;
    void incrementConnections();
    void decrementConnections();
    bool isHealthy() const;
    void setHealthy(bool healthy);

private:
    std::string host_;
    int port_;
    int activeConnections_ = 0;
    bool isHealthy_ = true;
};
