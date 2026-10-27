#include <gtest/gtest.h>
#include "least_connection_policy.h"
#include "round_robin_policy.h"

TEST(RoundRobinPolicyTest, RotatesAndWrapsAround) {
    BackendPool pool;
    pool.addBackend(BackendServer("127.0.0.1", 9001));
    pool.addBackend(BackendServer("127.0.0.1", 9002));
    RoundRobinPolicy policy;

    EXPECT_EQ(policy.selectBackend(pool), &pool.getBackend(0));
    EXPECT_EQ(policy.selectBackend(pool), &pool.getBackend(1));
    EXPECT_EQ(policy.selectBackend(pool), &pool.getBackend(0));
}

TEST(RoundRobinPolicyTest, HandlesEmptyAndSingleServerPools) {
    BackendPool pool;
    RoundRobinPolicy policy;
    EXPECT_EQ(policy.selectBackend(pool), nullptr);

    pool.addBackend(BackendServer("127.0.0.1", 9001));
    EXPECT_EQ(policy.selectBackend(pool), &pool.getBackend(0));
    EXPECT_EQ(policy.selectBackend(pool), &pool.getBackend(0));
}

TEST(LeastConnectionPolicyTest, ChoosesTheSmallestCount) {
    BackendPool pool;
    pool.addBackend(BackendServer("127.0.0.1", 9001));
    pool.addBackend(BackendServer("127.0.0.1", 9002));
    pool.addBackend(BackendServer("127.0.0.1", 9003));
    for (int count = 0; count < 3; ++count) {
        pool.getBackend(0).incrementConnections();
    }
    pool.getBackend(1).incrementConnections();
    for (int count = 0; count < 2; ++count) {
        pool.getBackend(2).incrementConnections();
    }

    LeastConnectionPolicy policy;
    EXPECT_EQ(policy.selectBackend(pool), &pool.getBackend(1));
}

TEST(LeastConnectionPolicyTest, HandlesEmptyPoolsAndKeepsFirstServerOnTies) {
    BackendPool pool;
    LeastConnectionPolicy policy;
    EXPECT_EQ(policy.selectBackend(pool), nullptr);

    pool.addBackend(BackendServer("127.0.0.1", 9001));
    pool.addBackend(BackendServer("127.0.0.1", 9002));
    EXPECT_EQ(policy.selectBackend(pool), &pool.getBackend(0));
}

TEST(SchedulingPolicyTest, CallsDerivedClassesThroughTheSameInterface) {
    BackendPool pool;
    pool.addBackend(BackendServer("127.0.0.1", 9001));
    pool.addBackend(BackendServer("127.0.0.1", 9002));
    pool.getBackend(0).incrementConnections();

    RoundRobinPolicy roundRobin;
    LeastConnectionPolicy leastConnections;
    SchedulingPolicy* policy = &roundRobin;
    EXPECT_EQ(policy->selectBackend(pool), &pool.getBackend(0));
    policy = &leastConnections;
    EXPECT_EQ(policy->selectBackend(pool), &pool.getBackend(1));
}
