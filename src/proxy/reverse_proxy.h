#pragma once
#include "http_request.h"
#include "path_router.h"
#include "socket.h"

class BackendServer;

class ReverseProxy {
public:
    explicit ReverseProxy(int port);
    void addRoute(const std::string& prefix, BackendPool& pool, SchedulingPolicy& policy);
    void run();

private:
    void handleClient(const Socket& client);
    HttpRequest readRequest(const Socket& client) const;
    std::string forwardRequest(const HttpRequest& request, const BackendServer& backend) const;

    int port_;
    PathRouter router_;
};
