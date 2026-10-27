#include <gtest/gtest.h>
#include "backend_pool.h"
#include <stdexcept>

TEST(BackendPoolTest, NewPoolIsEmpty) {
    BackendPool pool;
    EXPECT_EQ(pool.size(), 0u);
}

TEST(BackendPoolTest, StoresServerObjectsInOrder) {
    BackendPool pool;
    pool.addBackend(BackendServer("127.0.0.1", 9001));
    pool.addBackend(BackendServer("127.0.0.1", 9002));

    ASSERT_EQ(pool.size(), 2u);
    EXPECT_EQ(pool.getBackend(0).getPort(), 9001);
    EXPECT_EQ(pool.getBackend(1).getPort(), 9002);
}

TEST(BackendPoolTest, OwnsItsCopyOfTheServer) {
    BackendServer server("127.0.0.1", 9001);
    BackendPool pool;
    pool.addBackend(server);
    pool.getBackend(0).incrementConnections();
    EXPECT_EQ(pool.getBackend(0).getActiveConnections(), 1);
    EXPECT_EQ(server.getActiveConnections(), 0);
}

TEST(BackendPoolTest, InvalidIndexHasAClearFailure) {
    BackendPool pool;
    EXPECT_THROW(pool.getBackend(0), std::out_of_range);
}

