#include "reverse_proxy.h"
#include "backend_server.h"
#include "header_rewriter.h"
#include "least_connection_policy.h"
#include "round_robin_policy.h"
#include <iostream>
#include <utility>

namespace {
// Undo the count when this object leaves scope, including after an exception.
class BackendReservation {
public:
    explicit BackendReservation(BackendServer& backend) : backend_(backend) {
        backend_.incrementConnections();
    }

    ~BackendReservation() {
        backend_.decrementConnections();
    }

    BackendReservation(const BackendReservation&) = delete;
    BackendReservation& operator=(const BackendReservation&) = delete;

private:
    BackendServer& backend_;
};

class HttpError : public std::runtime_error {
public:
    HttpError(int status, const std::string& message) : std::runtime_error(message), status_(status) {}
    int status() const {
        return status_;
    }

private:
    int status_;
};

HttpRequest readRequest(const Socket& client, const HttpLimits& limits, Deadline deadline,
                        const volatile std::sig_atomic_t& stop) {
    std::string buffer;
    bool connectionClosed = false;
    while (true) {
        HttpRequest request;
        ParseResult result = parseRequest(buffer, request, limits, connectionClosed);
        if (result.status == ParseStatus::Complete) {
            return request;
        }
        if (result.status == ParseStatus::Error) {
            throw HttpError(result.errorStatus, result.error);
        }

        // TCP can split one HTTP request across several reads.
        bool receivedBytes = receiveMore(client, buffer, deadline, &stop);
        connectionClosed = !receivedBytes;
    }
}

HttpResponse readResponse(const Socket& upstream, const HttpLimits& limits, Deadline deadline,
                          const volatile std::sig_atomic_t& stop) {
    std::string buffer;
    bool connectionClosed = false;
    while (true) {
        HttpResponse response;
        ParseResult result = parseResponse(buffer, response, limits, connectionClosed);
        if (result.status == ParseStatus::Complete) {
            return response;
        }
        if (result.status == ParseStatus::Error) {
            throw HttpError(502, result.error);
        }

        bool receivedBytes = receiveMore(upstream, buffer, deadline, &stop);
        connectionClosed = !receivedBytes;
    }
}
}

ReverseProxy::ReverseProxy(ProxyConfig config) : config_(std::move(config)) {
    if (config_.ioTimeout.count() <= 0 || config_.connectTimeout.count() <= 0 ||
        config_.limits.maxHeaderBytes == 0) {
        throw std::invalid_argument("Proxy timeouts and header limit must be positive");
    }
    if (config_.listenPort < 1 || config_.listenPort > 65535) {
        throw std::invalid_argument("Invalid listen port");
    }

    for (const auto& route : config_.routes) {
        auto pool = std::make_unique<BackendPool>();
        std::unique_ptr<SchedulingPolicy> policy;
        if (route.useLeastConnections) {
            policy = std::make_unique<LeastConnectionPolicy>();
        } else {
            policy = std::make_unique<RoundRobinPolicy>();
        }

        for (const auto& endpoint : route.backends) {
            auto backend = std::make_unique<BackendServer>(endpoint.host, endpoint.port);
            backendAddresses_[backend.get()] = resolveBackend(endpoint.host, endpoint.port);
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
    bool contactingBackend = false;
    std::string stage = "client read";
    std::string endpoint = clientAddress;
    BackendServer* selectedBackend = nullptr;
    try {
        Deadline requestDeadline = std::chrono::steady_clock::now() + config_.ioTimeout;
        HttpRequest request = readRequest(client, config_.limits, requestDeadline, stop);

        stage = "route selection";
        const Route* route = router_.findRoute(request.target);
        if (route == nullptr) {
            throw HttpError(404, "No matching route");
        }
        selectedBackend = route->policy->selectBackend(route->pool->getBackends());
        if (selectedBackend == nullptr) {
            throw HttpError(503, "No healthy backend in matched pool");
        }

        contactingBackend = true;
        endpoint = selectedBackend->getHost() + ":" + std::to_string(selectedBackend->getPort());
        // This scope closes the backend socket and releases its count before replying.
        {
            BackendReservation reservation(*selectedBackend);

            stage = "upstream connect";
            const auto& addresses = backendAddresses_.at(selectedBackend);
            Deadline connectDeadline = std::chrono::steady_clock::now() + config_.connectTimeout;
            Socket upstream = connectBackend(addresses, connectDeadline, &stop);

            stage = "upstream send";
            std::string requestMessage = serializeRequest(request, clientAddress);
            Deadline sendDeadline = std::chrono::steady_clock::now() + config_.ioTimeout;
            sendAll(upstream, requestMessage, sendDeadline, &stop);

            stage = "upstream read";
            Deadline responseDeadline = std::chrono::steady_clock::now() + config_.ioTimeout;
            response = readResponse(upstream, config_.limits, responseDeadline, stop);
        }
        std::cout << request.method << " -> " << endpoint << " status=" << response.statusCode << std::endl;
    } catch (const HttpError& error) {
        if (stop) {
            return;
        }
        std::cerr << stage << " " << endpoint << ": " << error.what() << '\n';
        response = makeErrorResponse(error.status());
    } catch (const NetworkError& error) {
        if (stop) {
            return;
        }
        std::cerr << stage << " " << endpoint << ": " << error.what() << '\n';

        int status = 400; // Client connection failed.
        if (contactingBackend) {
            status = 502; // Backend connection failed.
            if (error.timedOut()) {
                status = 504;
            }
        } else if (error.timedOut()) {
            status = 408;
        }
        response = makeErrorResponse(status);
    }

    if (selectedBackend != nullptr) {
        std::cout << "Released " << endpoint << " active=" << selectedBackend->getActiveConnections() << std::endl;
    }
    if (!stop) {
        std::string responseMessage = serializeResponse(response);
        Deadline replyDeadline = std::chrono::steady_clock::now() + config_.ioTimeout;
        sendAll(client, responseMessage, replyDeadline, &stop);
    }
}

void ReverseProxy::run(const volatile std::sig_atomic_t& stop) {
    Socket listener = createListener(config_.listenPort);
    std::cout << "Proxy listening on port " << config_.listenPort << " (sequential HTTP demo)" << std::endl;
    while (!stop) {
        Socket client;
        std::string clientAddress;
        try {
            Deadline acceptDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
            client = acceptClient(listener, clientAddress, acceptDeadline, &stop);
        } catch (const NetworkError& error) {
            if (stop) {
                break;
            }
            // No client arrived yet; return to the loop and check the stop flag.
            if (error.timedOut()) {
                continue;
            }
            throw;
        }

        // Finish this client's exchange before accepting the next client.
        try {
            handleClient(client, clientAddress, stop);
        } catch (const NetworkError& error) {
            if (!stop) {
                std::cerr << "Client send " << clientAddress << ": " << error.what() << '\n';
            }
        }
    }
    std::cout << "Proxy stopped; sockets released" << std::endl;
}
