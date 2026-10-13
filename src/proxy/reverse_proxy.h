#pragma once
#include <chrono>
#include <csignal>
#include "http_parser.h"
#include "socket.h"
#include "backend_pool.h"
#include "path_router.h"
#include "scheduling_policy.h"
#include <memory>
#include <unordered_map>

struct BackendEndpoint {
    std::string host;
    int port;
};

struct RouteConfig {
    std::string prefix;
    std::vector<BackendEndpoint> backends;
    bool useLeastConnections = false;
};

struct ProxyConfig {
    int listenPort = 8080;
    std::chrono::milliseconds connectTimeout{1000};
    std::chrono::milliseconds ioTimeout{5000};
    HttpLimits limits;
    std::vector<RouteConfig> routes;
};

class ReverseProxy {
public:
    explicit ReverseProxy(ProxyConfig config);
    void run(const volatile std::sig_atomic_t& stop);
private:
    ProxyConfig config_;
    std::vector<std::unique_ptr<BackendPool>> pools_;
    std::vector<std::unique_ptr<SchedulingPolicy>> policies_;
    PathRouter router_;
    std::unordered_map<BackendServer*, std::vector<SocketAddress>> backendAddresses_;
    void handleClient(const Socket& client, const std::string& clientAddress,
                      const volatile std::sig_atomic_t& stop);
};
