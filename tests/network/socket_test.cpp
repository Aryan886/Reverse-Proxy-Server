#include <gtest/gtest.h>
#include "socket.h"
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

TEST(SocketTest, DestructorClosesTheConnectionAfterOwnershipTransfer) {
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
    Socket peer(descriptors[1]);
    {
        Socket original(descriptors[0]);
        Socket owner(std::move(original));
        EXPECT_EQ(original.get(), -1);
        EXPECT_EQ(owner.get(), descriptors[0]);
    }
    errno = 0;
    EXPECT_EQ(::fcntl(descriptors[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(SocketTest, SendsDataAndRecognizesAClosedConnection) {
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
    Socket sender(descriptors[0]);
    Socket receiver(descriptors[1]);
    std::string message("hello\0world", 11);
    sender.sendAll(message);
    ASSERT_EQ(::shutdown(sender.get(), SHUT_WR), 0);

    std::string received;
    while (true) {
        std::string part = receiver.receive();
        if (part.empty()) {
            break;
        }
        received += part;
    }
    EXPECT_EQ(received, message);
}

TEST(SocketTest, DisconnectedPeerRaisesAnError) {
    int descriptors[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
    Socket sender(descriptors[0]);
    ::close(descriptors[1]);
    EXPECT_THROW(sender.sendAll("hello"), std::runtime_error);
}

TEST(SocketTest, RejectsUnsupportedAddressesAndInvalidPorts) {
    EXPECT_THROW(Socket::connectTo("localhost", 9001), std::invalid_argument);
    EXPECT_THROW(Socket::connectTo("127.0.0.1", 0), std::invalid_argument);
    EXPECT_THROW(Socket::listenOn(65536), std::invalid_argument);
}
