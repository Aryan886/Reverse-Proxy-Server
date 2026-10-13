#include "socket.h"
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <memory>
#include <unistd.h>
#include <utility>

namespace {
NetworkError socketError(const std::string& operation) {
    return NetworkError(operation + ": " + std::strerror(errno));
}
}

Socket::~Socket() {
    // Linux releases the descriptor even if close reports EINTR; retry could close a reused fd.
    if (descriptor_ >= 0) ::close(descriptor_);
}

Socket::Socket(Socket&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (descriptor_ >= 0) ::close(descriptor_);
        descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
}

Socket createListener(int port) {
    if (port < 1 || port > 65535) throw std::invalid_argument("Listen port must be between 1 and 65535");
    Socket listener(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (listener.get() < 0) throw socketError("socket listener");
    int reuse = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        throw socketError("setsockopt SO_REUSEADDR");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw socketError("bind port " + std::to_string(port));
    }
    if (::listen(listener.get(), 32) < 0) throw socketError("listen port " + std::to_string(port));
    return listener;
}

void waitForSocket(int descriptor, short events, Deadline deadline,
                   const volatile std::sig_atomic_t* stop) {
    if (descriptor < 0) throw NetworkError("wait: invalid socket descriptor");
    while (true) {
        if (stop && *stop) throw NetworkError("Socket operation cancelled during shutdown");
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) throw NetworkError("Socket stage deadline exceeded", true);
        pollfd descriptorState{descriptor, events, 0};
        int timeout = remaining > 200 ? 200 : static_cast<int>(remaining);
        int result = ::poll(&descriptorState, 1, timeout);
        if (result < 0) {
            if (errno == EINTR) continue;
            throw socketError("poll");
        }
        if (result == 0) continue;
        if (descriptorState.revents & POLLNVAL) throw NetworkError("poll: invalid descriptor");
        // Let recv/send/SO_ERROR report EOF or the precise error, including on hangup.
        if (descriptorState.revents & (events | POLLERR | POLLHUP)) return;
    }
}

void sendAll(const Socket& socket, const std::string& message, Deadline deadline,
             const volatile std::sig_atomic_t* stop) {
    std::size_t bytesSent = 0;
    while (bytesSent < message.size()) {
        waitForSocket(socket.get(), POLLOUT, deadline, stop);
        ssize_t count = ::send(socket.get(), message.data() + bytesSent,
                               message.size() - bytesSent, MSG_NOSIGNAL);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            throw socketError("send");
        }
        if (count == 0) throw NetworkError("send: peer made no progress");
        bytesSent += static_cast<std::size_t>(count);
    }
}

std::vector<SocketAddress> resolveBackend(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    int error = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (error != 0) throw NetworkError("resolve " + host + ":" + std::to_string(port) + ": " + ::gai_strerror(error));
    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> owner(result, ::freeaddrinfo);
    std::vector<SocketAddress> addresses;
    for (auto current = result; current; current = current->ai_next) {
        if (current->ai_addrlen > sizeof(sockaddr_storage)) continue;
        SocketAddress address;
        std::memcpy(&address.address, current->ai_addr, current->ai_addrlen);
        address.length = current->ai_addrlen;
        addresses.push_back(address);
    }
    if (addresses.empty()) throw NetworkError("resolve " + host + ": no usable addresses");
    return addresses;
}

Socket connectBackend(const std::vector<SocketAddress>& addresses, Deadline deadline,
                      const volatile std::sig_atomic_t* stop) {
    std::string lastError = "No resolved backend addresses";
    for (const auto& address : addresses) {
        if (stop && *stop) throw NetworkError("Backend connect cancelled during shutdown");
        if (std::chrono::steady_clock::now() >= deadline) throw NetworkError("Backend connect deadline exceeded", true);
        Socket socket(::socket(address.address.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
        if (socket.get() < 0) throw socketError("socket upstream");
        int result = ::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address.address), address.length);
        if (result == 0) return socket;
        if (errno == EINPROGRESS || errno == EINTR) {
            waitForSocket(socket.get(), POLLOUT, deadline, stop);
            int error = 0;
            socklen_t length = sizeof(error);
            if (::getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, &error, &length) < 0) throw socketError("getsockopt SO_ERROR");
            if (error == 0) return socket;
            lastError = std::strerror(error);
        } else {
            lastError = std::strerror(errno);
        }
    }
    throw NetworkError("Backend connect: " + lastError);
}
