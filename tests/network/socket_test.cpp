#include <gtest/gtest.h>
#include "socket.h"
#include <cerrno>
#include <fcntl.h>
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
