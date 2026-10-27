#include "reverse_proxy.h"
#include "backend_pool.h"
#include "round_robin_policy.h"
#include <exception>
#include <iostream>

int main() {
    try {
        BackendPool apiServers;
        apiServers.addBackend(BackendServer("127.0.0.1", 9001));
        apiServers.addBackend(BackendServer("127.0.0.1", 9002));

        BackendPool staticServers;
        staticServers.addBackend(BackendServer("127.0.0.1", 9003));

        RoundRobinPolicy apiPolicy;
        RoundRobinPolicy staticPolicy;

        // Create the shared objects first so they outlive the proxy.
        ReverseProxy proxy(8080);
        proxy.addRoute("/api", apiServers, apiPolicy);
        proxy.addRoute("/static", staticServers, staticPolicy);
        proxy.run();
    } catch (const std::exception& error) {
        std::cerr << "Proxy stopped: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
