#include "socket.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
std::runtime_error socketError(const std::string& operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}
}

Socket::Socket(int descriptor) : descriptor_(descriptor) {
    if (descriptor_ < 0) {
        throw socketError("Could not open socket");
    }
}

Socket::~Socket() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
    }
}

Socket::Socket(Socket&& other) noexcept : descriptor_(other.descriptor_) {
    // Only one object should close a connection.
    other.descriptor_ = -1;
}

int Socket::get() const {
    return descriptor_;
}

void Socket::setTimeout() const {
    timeval timeout{};
    timeout.tv_sec = 5;
    if (::setsockopt(descriptor_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        ::setsockopt(descriptor_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        throw socketError("Could not set socket timeout");
    }
}

Socket Socket::listenOn(int port) {
    if (port < 1 || port > 65535) {
        throw std::invalid_argument("Listen port must be between 1 and 65535");
    }
    Socket listener(::socket(AF_INET, SOCK_STREAM, 0));
    int reuse = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        throw socketError("Could not allow port reuse");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw socketError("Could not bind port " + std::to_string(port));
    }
    if (::listen(listener.get(), 10) < 0) {
        throw socketError("Could not listen");
    }
    return listener;
}

Socket Socket::acceptClient() const {
    Socket client(::accept(descriptor_, nullptr, nullptr));
    client.setTimeout();
    return client;
}

Socket Socket::connectTo(const std::string& host, int port) {
    if (port < 1 || port > 65535) {
        throw std::invalid_argument("Backend port must be between 1 and 65535");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        throw std::invalid_argument("Use an IPv4 address such as 127.0.0.1");
    }
    Socket connection(::socket(AF_INET, SOCK_STREAM, 0));
    connection.setTimeout();
    if (::connect(connection.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw socketError("Could not connect to backend");
    }
    return connection;
}

void Socket::sendAll(const std::string& message) const {
    std::size_t bytesSent = 0;
    while (bytesSent < message.size()) {
        // A single send may write only part of the message.
        ssize_t count = ::send(descriptor_, message.data() + bytesSent,
                               message.size() - bytesSent, MSG_NOSIGNAL);
        if (count < 0) {
            throw socketError("Could not send data");
        }
        if (count == 0) {
            throw std::runtime_error("Connection closed while sending data");
        }
        bytesSent += static_cast<std::size_t>(count);
    }
}

std::string Socket::receive() const {
    char buffer[4096];
    ssize_t count = ::recv(descriptor_, buffer, sizeof(buffer), 0);
    if (count < 0) {
        throw socketError("Could not receive data (connection failed or timed out)");
    }
    return std::string(buffer, static_cast<std::size_t>(count));
}
