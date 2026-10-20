#pragma once
#include <chrono>
#include <csignal>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/socket.h>

// An absolute finish time, so receiving another byte does not restart a timeout.
using Deadline = std::chrono::steady_clock::time_point;

class NetworkError : public std::runtime_error {
public:
    NetworkError(const std::string& message, bool timedOut = false)
        : std::runtime_error(message), timedOut_(timedOut) {}
    bool timedOut() const {
        return timedOut_;
    }

private:
    bool timedOut_;
};

// Owns one OS descriptor and closes it when the object leaves scope.
class Socket {
public:
    explicit Socket(int descriptor = -1) : descriptor_(descriptor) {}
    ~Socket();
    // Two owners must never close the same descriptor. Allow transfer, not copying.
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    int get() const {
        return descriptor_;
    }

private:
    int descriptor_;
};

struct SocketAddress {
    sockaddr_storage address{};
    socklen_t length = 0;
};

Socket createListener(int port);
Socket acceptClient(const Socket& listener, std::string& clientAddress, Deadline deadline,
                    const volatile std::sig_atomic_t* stop = nullptr);
std::vector<SocketAddress> resolveBackend(const std::string& host, int port);
Socket connectBackend(const std::vector<SocketAddress>& addresses, Deadline deadline,
                      const volatile std::sig_atomic_t* stop = nullptr);
void waitForSocket(int descriptor, short events, Deadline deadline,
                   const volatile std::sig_atomic_t* stop = nullptr);
void sendAll(const Socket& socket, const std::string& message, Deadline deadline,
             const volatile std::sig_atomic_t* stop = nullptr);

// Appends the next bytes to buffer. Returns false when the peer has closed its send side.
bool receiveMore(const Socket& socket, std::string& buffer, Deadline deadline,
                 const volatile std::sig_atomic_t* stop = nullptr);
