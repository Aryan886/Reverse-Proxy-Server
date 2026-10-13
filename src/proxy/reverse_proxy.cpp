#include "reverse_proxy.h"
#include "backend_server.h"
#include "header_rewriter.h"
#include "least_connection_policy.h"
#include "round_robin_policy.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <utility>

namespace {
class BackendReservation {
public:
    explicit BackendReservation(BackendServer& backend) : backend_(backend) { backend_.incrementConnections(); }
    ~BackendReservation() { backend_.decrementConnections(); }
    BackendReservation(const BackendReservation&) = delete;
    BackendReservation& operator=(const BackendReservation&) = delete;
private:
    BackendServer& backend_;
};

class HttpError : public std::runtime_error {
public:
    HttpError(int status, const std::string& message) : std::runtime_error(message), status_(status) {}
    int status() const { return status_; }
private:
    int status_;
};

bool readMore(const Socket& socket, std::string& buffer, Deadline deadline,
              const volatile std::sig_atomic_t& stop) {
    while (true) {
        waitForSocket(socket.get(), POLLIN, deadline, &stop);
        char bytes[4096];
        ssize_t count = ::recv(socket.get(), bytes, sizeof(bytes), 0);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            throw NetworkError(std::string("recv: ") + std::strerror(errno));
        }
        if (count == 0) return false;
        buffer.append(bytes, static_cast<std::size_t>(count));
        return true;
    }
}

HttpRequest readRequest(const Socket& client, const HttpLimits& limits, Deadline deadline,
                        const volatile std::sig_atomic_t& stop) {
    std::string buffer;
    bool eof = false;
    while (true) {
        HttpRequest request;
        auto result = parseRequest(buffer, request, limits, eof);
        if (result.status == ParseStatus::Complete) return request;
        if (result.status == ParseStatus::Error) throw HttpError(result.errorStatus, result.error);
        eof = !readMore(client, buffer, deadline, stop);
    }
}

HttpResponse readResponse(const Socket& upstream, const HttpLimits& limits, Deadline deadline,
                          const volatile std::sig_atomic_t& stop) {
    std::string buffer;
    bool eof = false;
    while (true) {
        HttpResponse response;
        auto result = parseResponse(buffer, response, limits, eof);
        if (result.status == ParseStatus::Complete) return response;
        if (result.status == ParseStatus::Error) throw HttpError(502, result.error);
        eof = !readMore(upstream, buffer, deadline, stop);
    }
}
}

ReverseProxy::ReverseProxy(ProxyConfig config) : config_(std::move(config)) {
    if (config_.ioTimeout.count() <= 0 || config_.connectTimeout.count() <= 0 ||
        config_.limits.maxHeaderBytes == 0) {
        throw std::invalid_argument("Proxy timeouts and header limit must be positive");
    }
    if (config_.listenPort < 1 || config_.listenPort > 65535) throw std::invalid_argument("Invalid listen port");
    for (const auto& route : config_.routes) {
        auto pool = std::make_unique<BackendPool>();
        std::unique_ptr<SchedulingPolicy> policy;
        if (route.useLeastConnections) policy = std::make_unique<LeastConnectionPolicy>();
        else policy = std::make_unique<RoundRobinPolicy>();
        for (const auto& endpoint : route.backends) {
            auto backend = std::make_unique<BackendServer>(endpoint.host, endpoint.port);
            backendAddresses_.emplace(backend.get(), resolveBackend(endpoint.host, endpoint.port));
            pool->addBackend(std::move(backend));
        }
        router_.addRoute(route.prefix, *pool, *policy);
        pools_.push_back(std::move(pool));
        policies_.push_back(std::move(policy));
    }
}

void ReverseProxy::handleClient(const Socket& client, const std::string& clientAddress,
                                const volatile std::sig_atomic_t& stop) {
    HttpResponse response;
    bool upstreamStage = false;
    std::string stage = "client read";
    std::string endpoint = clientAddress;
    BackendServer* selected = nullptr;
    try {
        auto request = readRequest(client, config_.limits, std::chrono::steady_clock::now() + config_.ioTimeout, stop);
        stage = "route selection";
        const Route* route = router_.findRoute(request.target);
        if (!route) throw HttpError(404, "No matching route");
        selected = route->policy->selectBackend(route->pool->getBackends());
        if (!selected) throw HttpError(503, "No healthy backend in matched pool");
        upstreamStage = true;
        endpoint = selected->getHost() + ":" + std::to_string(selected->getPort());
        {
            BackendReservation reservation(*selected);
            stage = "upstream connect";
            Socket upstream = connectBackend(backendAddresses_.at(selected), std::chrono::steady_clock::now() + config_.connectTimeout, &stop);
            stage = "upstream send";
            sendAll(upstream, serializeRequest(request, clientAddress), std::chrono::steady_clock::now() + config_.ioTimeout, &stop);
            stage = "upstream read";
            response = readResponse(upstream, config_.limits, std::chrono::steady_clock::now() + config_.ioTimeout, stop);
        }
        std::cout << request.method << " -> " << endpoint << " status=" << response.statusCode << std::endl;
    } catch (const HttpError& error) {
        if (stop) return;
        std::cerr << stage << " " << endpoint << ": " << error.what() << '\n';
        response = makeErrorResponse(error.status());
    } catch (const NetworkError& error) {
        if (stop) return;
        std::cerr << stage << " " << endpoint << ": " << error.what() << '\n';
        int status = upstreamStage ? (error.timedOut() ? 504 : 502) : (error.timedOut() ? 408 : 400);
        response = makeErrorResponse(status);
    }
    if (selected) std::cout << "Released " << endpoint << " active=" << selected->getActiveConnections() << std::endl;
    if (!stop) sendAll(client, serializeResponse(response), std::chrono::steady_clock::now() + config_.ioTimeout, &stop);
}

void ReverseProxy::run(const volatile std::sig_atomic_t& stop) {
    Socket listener = createListener(config_.listenPort);
    std::cout << "Proxy listening on port " << config_.listenPort << " (sequential HTTP demo)" << std::endl;
    while (!stop) {
        try {
            waitForSocket(listener.get(), POLLIN, std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(200), &stop);
        } catch (const NetworkError& error) {
            if (stop) break;
            if (error.timedOut()) continue;
            throw;
        }
        sockaddr_in address{};
        socklen_t addressLength = sizeof(address);
        Socket client(::accept4(listener.get(), reinterpret_cast<sockaddr*>(&address), &addressLength,
                                SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (client.get() < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            throw NetworkError(std::string("accept: ") + std::strerror(errno));
        }
        char clientAddress[INET_ADDRSTRLEN];
        if (!::inet_ntop(AF_INET, &address.sin_addr, clientAddress, sizeof(clientAddress))) {
            throw NetworkError(std::string("inet_ntop client address: ") + std::strerror(errno));
        }
        try {
            handleClient(client, clientAddress, stop);
        } catch (const NetworkError& error) {
            if (!stop) std::cerr << "Client send " << clientAddress << ": " << error.what() << '\n';
        }
    }
    std::cout << "Proxy stopped; sockets released" << std::endl;
}
