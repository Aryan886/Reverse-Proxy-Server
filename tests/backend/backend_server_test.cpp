#include <gtest/gtest.h>
#include "backend_server.h"
#include <stdexcept>

TEST(BackendServerTest, StartsHealthyWithNoConnections) {
    BackendServer server("127.0.0.1", 9001);
    EXPECT_EQ(server.getHost(), "127.0.0.1");
    EXPECT_EQ(server.getPort(), 9001);
    EXPECT_EQ(server.getActiveConnections(), 0);
    EXPECT_TRUE(server.isHealthy());
}

TEST(BackendServerTest, RejectsInvalidEndpoints) {
    EXPECT_THROW(BackendServer("", 9001), std::invalid_argument);
    EXPECT_THROW(BackendServer("localhost", 0), std::invalid_argument);
    EXPECT_THROW(BackendServer("localhost", 65536), std::invalid_argument);
    EXPECT_THROW(BackendServer("localhost", -1), std::invalid_argument);
}

TEST(BackendServerTest, ChangesHealth) {
    BackendServer server("localhost", 9001);
    server.setHealthy(false);
    EXPECT_FALSE(server.isHealthy());
    server.setHealthy(true);
    EXPECT_TRUE(server.isHealthy());
}

TEST(BackendServerTest, CountsNeverBecomeNegative) {
    BackendServer server("localhost", 9001);
    server.decrementConnections();
    EXPECT_EQ(server.getActiveConnections(), 0);
    server.incrementConnections();
    server.incrementConnections();
    server.decrementConnections();
    EXPECT_EQ(server.getActiveConnections(), 1);
    server.decrementConnections();
    server.decrementConnections();
    EXPECT_EQ(server.getActiveConnections(), 0);
}
