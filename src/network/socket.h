#pragma once
#include <chrono>
#include <csignal>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/socket.h>

using Deadline = std::chrono::steady_clock::time_point;

class NetworkError : public std::runtime_error {
public:
    NetworkError(const std::string& message, bool timedOut = false)
        : std::runtime_error(message), timedOut_(timedOut) {}
    bool timedOut() const { return timedOut_; }
private:
    bool timedOut_;
};

class Socket {
public:
    explicit Socket(int descriptor = -1) : descriptor_(descriptor) {}
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    int get() const { return descriptor_; }
private:
    int descriptor_;
};

Socket createListener(int port);
struct SocketAddress {
    sockaddr_storage address{};
    socklen_t length = 0;
};

std::vector<SocketAddress> resolveBackend(const std::string& host, int port);
Socket connectBackend(const std::vector<SocketAddress>& addresses, Deadline deadline,
                      const volatile std::sig_atomic_t* stop = nullptr);
void waitForSocket(int descriptor, short events, Deadline deadline,
                   const volatile std::sig_atomic_t* stop = nullptr);
void sendAll(const Socket& socket, const std::string& message, Deadline deadline,
             const volatile std::sig_atomic_t* stop = nullptr);
