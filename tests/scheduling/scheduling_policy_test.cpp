#include <gtest/gtest.h>
#include "backend_server.h"
#include "least_connection_policy.h"
#include "round_robin_policy.h"

TEST(LeastConnectionPolicyTest, SelectsMinimumAfterExaminingEveryBackend) {
    BackendServer first("localhost", 9001);
    BackendServer second("localhost", 9002);
    BackendServer third("localhost", 9003);
    for (int count = 0; count < 3; ++count) first.incrementConnections();
    second.incrementConnections();
    for (int count = 0; count < 2; ++count) third.incrementConnections();
    LeastConnectionPolicy policy;
    EXPECT_EQ(policy.selectBackend({&first, &second, &third}), &second);
}

TEST(RoundRobinPolicyTest, RotatesThroughEveryServer) {
    BackendServer first("localhost", 9001), second("localhost", 9002), third("localhost", 9003);
    RoundRobinPolicy policy;
    std::vector<BackendServer*> servers{&first, &second, &third};
    EXPECT_EQ(policy.selectBackend(servers), &first);
    EXPECT_EQ(policy.selectBackend(servers), &second);
    EXPECT_EQ(policy.selectBackend(servers), &third);
    EXPECT_EQ(policy.selectBackend(servers), &first);
}

TEST(RoundRobinPolicyTest, HandlesEmptySingleAndChangedSize) {
    BackendServer first("localhost", 9001), second("localhost", 9002);
    RoundRobinPolicy policy;
    EXPECT_EQ(policy.selectBackend({}), nullptr);
    EXPECT_EQ(policy.selectBackend({&first, &second}), &first);
    EXPECT_EQ(policy.selectBackend({&first}), &first);
    EXPECT_EQ(policy.selectBackend({&first}), &first);
}

TEST(RoundRobinPolicyTest, SkipsNullAndUnhealthyServers) {
    BackendServer first("localhost", 9001), second("localhost", 9002), third("localhost", 9003);
    second.setHealthy(false);
    RoundRobinPolicy policy;
    std::vector<BackendServer*> servers{&first, nullptr, &second, &third};
    EXPECT_EQ(policy.selectBackend(servers), &first);
    EXPECT_EQ(policy.selectBackend(servers), &third);
    EXPECT_EQ(policy.selectBackend(servers), &first);
    first.setHealthy(false);
    third.setHealthy(false);
    EXPECT_EQ(policy.selectBackend(servers), nullptr);
}

TEST(LeastConnectionPolicyTest, KeepsFirstEligibleOnTies) {
    BackendServer first("localhost", 9001), second("localhost", 9002);
    LeastConnectionPolicy policy;
    EXPECT_EQ(policy.selectBackend({nullptr, &first, &second}), &first);
    first.setHealthy(false);
    EXPECT_EQ(policy.selectBackend({&first, &second}), &second);
    second.setHealthy(false);
    EXPECT_EQ(policy.selectBackend({nullptr, &first, &second}), nullptr);
    EXPECT_EQ(policy.selectBackend({}), nullptr);
}

TEST(LeastConnectionPolicyTest, SkipsUnhealthyMinimum) {
    BackendServer first("localhost", 9001), second("localhost", 9002);
    first.setHealthy(false);
    second.incrementConnections();
    LeastConnectionPolicy policy;
    EXPECT_EQ(policy.selectBackend({&first, &second}), &second);
}
