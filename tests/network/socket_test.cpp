#include <gtest/gtest.h>
#include "socket.h"
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <thread>
#include <exception>

TEST(SocketTest, MoveTransfersOwnershipAndDestructionCloses) {
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
    Socket peer(descriptors[1]);
    {
        Socket original(descriptors[0]);
        Socket owner(std::move(original));
        EXPECT_EQ(original.get(), -1);
        EXPECT_EQ(owner.get(), descriptors[0]);
        Socket replacement;
        replacement = std::move(owner);
        EXPECT_EQ(owner.get(), -1);
    }
    errno = 0;
    EXPECT_EQ(::fcntl(descriptors[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(SocketTest, PeerDisconnectRaisesInsteadOfTerminatingProcess) {
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, descriptors), 0);
    Socket sender(descriptors[0]);
    ::close(descriptors[1]);
    EXPECT_THROW(sendAll(sender, "hello", std::chrono::steady_clock::now() +
                         std::chrono::seconds(1)), NetworkError);
}

TEST(SocketTest, ReceiveAppendsBinaryBytesAndReportsEndOfStream) {
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, descriptors), 0);
    Socket sender(descriptors[0]), receiver(descriptors[1]);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    std::string message("a\0b", 3);
    sendAll(sender, message, deadline);
    ASSERT_EQ(::shutdown(sender.get(), SHUT_WR), 0);

    std::string buffer = "existing:";
    while (receiveMore(receiver, buffer, deadline)) {
        // Read through EOF; TCP is allowed to split the message into smaller reads.
    }
    EXPECT_EQ(buffer, std::string("existing:") + message);
}

TEST(SocketTest, ReceiveDistinguishesTimeoutFromShutdown) {
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, descriptors), 0);
    Socket sender(descriptors[0]), receiver(descriptors[1]);
    std::string buffer = "unchanged";
    try {
        receiveMore(receiver, buffer, std::chrono::steady_clock::now());
        FAIL() << "Expected a receive timeout";
    } catch (const NetworkError& error) {
        EXPECT_TRUE(error.timedOut());
    }

    volatile std::sig_atomic_t stop = 1;
    try {
        receiveMore(receiver, buffer, std::chrono::steady_clock::now() + std::chrono::seconds(1), &stop);
        FAIL() << "Expected shutdown to cancel the receive";
    } catch (const NetworkError& error) {
        EXPECT_FALSE(error.timedOut());
    }
    EXPECT_EQ(buffer, "unchanged");
}

TEST(SocketTest, AcceptReportsPeerAddressAndReturnsUsableNonblockingSocket) {
    // Let the OS choose a free port so the test does not depend on a fixed port.
    Socket listener(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    ASSERT_GE(listener.get(), 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    ASSERT_EQ(::listen(listener.get(), 1), 0);
    socklen_t addressLength = sizeof(address);
    ASSERT_EQ(::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &addressLength), 0);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    auto addresses = resolveBackend("127.0.0.1", ntohs(address.sin_port));
    Socket sender = connectBackend(addresses, deadline);
    std::string clientAddress;
    Socket client = acceptClient(listener, clientAddress, deadline);
    EXPECT_EQ(clientAddress, "127.0.0.1");
    int flags = ::fcntl(client.get(), F_GETFL);
    ASSERT_GE(flags, 0);
    EXPECT_NE(flags & O_NONBLOCK, 0);
    int descriptorFlags = ::fcntl(client.get(), F_GETFD);
    ASSERT_GE(descriptorFlags, 0);
    EXPECT_NE(descriptorFlags & FD_CLOEXEC, 0);

    sendAll(sender, "hello", deadline);
    ASSERT_EQ(::shutdown(sender.get(), SHUT_WR), 0);
    std::string buffer;
    while (receiveMore(client, buffer, deadline)) {
        // Collect all bytes before checking the complete message.
    }
    EXPECT_EQ(buffer, "hello");
}

TEST(SocketTest, PastDeadlineFailsImmediately) {
    EXPECT_THROW(sendAll(Socket{}, "hello", std::chrono::steady_clock::now() + std::chrono::seconds(1)), NetworkError);
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
    Socket sender(descriptors[0]), peer(descriptors[1]);
    try {
        sendAll(sender, "hello", std::chrono::steady_clock::now());
        FAIL() << "Expected expired deadline";
    } catch (const NetworkError& error) {
        EXPECT_TRUE(error.timedOut());
    }
}

TEST(SocketTest, SmallSendBufferAndSlowReaderPreserveEveryByte) {
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, descriptors), 0);
    Socket sender(descriptors[0]), peer(descriptors[1]);
    int bufferSize = 1024;
    ASSERT_EQ(::setsockopt(sender.get(), SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize)), 0);
    std::string message(128 * 1024, 'x');
    message[1234] = '\0';
    std::string received;
    std::exception_ptr readerError;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::thread reader([&] {
        try {
            while (received.size() < message.size()) {
                waitForSocket(peer.get(), POLLIN, deadline);
                char bytes[257];
                ssize_t count = ::recv(peer.get(), bytes, sizeof(bytes), 0);
                if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
                if (count <= 0) throw NetworkError("Test reader: EOF or failed recv");
                received.append(bytes, static_cast<std::size_t>(count));
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        } catch (...) {
            readerError = std::current_exception();
        }
    });
    EXPECT_NO_THROW(sendAll(sender, message, deadline));
    reader.join();
    EXPECT_FALSE(readerError);
    EXPECT_EQ(received, message);
}
