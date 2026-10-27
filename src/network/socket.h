#pragma once
#include <string>

class Socket {
public:
    explicit Socket(int descriptor);
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;

    static Socket listenOn(int port);
    static Socket connectTo(const std::string& host, int port);
    Socket acceptClient() const;
    void sendAll(const std::string& message) const;
    std::string receive() const;
    int get() const;

private:
    void setTimeout() const;
    int descriptor_;
};
