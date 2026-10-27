#include "reverse_proxy.h"
#include "backend_pool.h"
#include "http_parser.h"
#include "scheduling_policy.h"
#include <iostream>
#include <stdexcept>

ReverseProxy::ReverseProxy(int port) : port_(port) {}

void ReverseProxy::addRoute(const std::string& prefix, BackendPool& pool, SchedulingPolicy& policy) {
    router_.addRoute(prefix, pool, policy);
}

HttpRequest ReverseProxy::readRequest(const Socket& client) const {
    std::string message;
    while (message.find("\r\n\r\n") == std::string::npos) {
        std::string part = client.receive();
        if (part.empty()) {
            throw std::runtime_error("Client disconnected before sending a complete request");
        }
        message += part;
        if (message.size() > 8192) {
            throw std::runtime_error("Request exceeds the 8 KiB demo limit");
        }
    }
    return parseRequest(message);
}

std::string ReverseProxy::forwardRequest(const HttpRequest& request, const BackendServer& backend) const {
    Socket connection = Socket::connectTo(backend.getHost(), backend.getPort());
    connection.sendAll(makeBackendRequest(request, backend.getHost(), backend.getPort()));

    std::string response;
    while (true) {
        std::string part = connection.receive();
        if (part.empty()) {
            break;
        }
        response += part;
        if (response.size() > 1024 * 1024) {
            throw std::runtime_error("Backend response exceeds the 1 MiB demo limit");
        }
    }
    if (response.empty()) {
        throw std::runtime_error("Backend returned an empty response");
    }
    return response;
}

void ReverseProxy::handleClient(const Socket& client) {
    HttpRequest request;
    try {
        request = readRequest(client);
    } catch (const std::exception& error) {
        std::cerr << "Request failed: " << error.what() << '\n';
        client.sendAll(makeErrorResponse(400, "Bad Request"));
        return;
    }
    if (request.method != "GET") {
        client.sendAll(makeErrorResponse(405, "Method Not Allowed"));
        return;
    }

    const Route* route = router_.findRoute(request.target);
    if (route == nullptr) {
        client.sendAll(makeErrorResponse(404, "Not Found"));
        return;
    }
    BackendServer* backend = route->policy->selectBackend(*route->pool);
    if (backend == nullptr) {
        client.sendAll(makeErrorResponse(503, "Service Unavailable"));
        return;
    }

    std::string response;
    backend->incrementConnections();
    try {
        response = forwardRequest(request, *backend);
        std::cout << request.target << " -> " << backend->getHost()
                  << ':' << backend->getPort() << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "Backend " << backend->getHost() << ':' << backend->getPort()
                  << " failed: " << error.what() << '\n';
        response = makeErrorResponse(502, "Bad Gateway");
    }
    backend->decrementConnections();
    client.sendAll(response);
}

void ReverseProxy::run() {
    Socket listener = Socket::listenOn(port_);
    std::cout << "Proxy ready at http://127.0.0.1:" << port_
              << " (GET requests only). Press Ctrl+C to stop." << std::endl;
    while (true) {
        Socket client = listener.acceptClient();
        try {
            handleClient(client);
        } catch (const std::exception& error) {
            std::cerr << "Could not reply to client: " << error.what() << '\n';
        }
    }
}
