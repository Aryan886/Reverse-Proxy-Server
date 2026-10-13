#include <gtest/gtest.h>
#include "backend_pool.h"
#include "backend_server.h"
#include <stdexcept>

/*
Fix : provides a frehs backendPool to every test so tests 
don't lead state into each other
*/

class BackendPoolTest : public :: testing::Test{
    protected:
        BackendPool pool;
};

//test 1 : a newly constructed pool which has nothing in it
TEST_F(BackendPoolTest, NewPoolIsEmpty){
    EXPECT_EQ(pool.size(), 0);
    EXPECT_TRUE(pool.getBackends().empty());
}

//test 2 : adding one backend updates size and returns correct data
TEST_F(BackendPoolTest, AddSingleBackend){
    pool.addBackend(std::make_unique<BackendServer>("127.0.0.1", 8080));

    EXPECT_EQ(pool.size(), 1);
    //asserts stop test if false, next line would crash
    ASSERT_EQ(pool.getBackends().size(), 1);
    
    BackendServer* fetched = pool.getBackends()[0];
    EXPECT_EQ(fetched->getHost(), "127.0.0.1");
    EXPECT_EQ(fetched->getPort(), 8080);
}

//test 3 : adding multiple backends and verifying count and correctness
TEST_F(BackendPoolTest, AddMultipleBackends){
    pool.addBackend(std::make_unique<BackendServer>("10.0.0.1", 8081));
    pool.addBackend(std::make_unique<BackendServer>("10.0.0.2", 8082));
    pool.addBackend(std::make_unique<BackendServer>("10.0.0.3", 8083));

    EXPECT_EQ(pool.size(), 3);
    ASSERT_EQ(pool.getBackends().size(), 3);

    EXPECT_EQ(pool.getBackends()[0]->getHost(), "10.0.0.1");
    EXPECT_EQ(pool.getBackends()[1]->getHost(), "10.0.0.2");
    EXPECT_EQ(pool.getBackends()[2]->getHost(), "10.0.0.3");
}

//test 4 : removing an existing backend - success path, verify sync
TEST_F(BackendPoolTest, RemoveExistingBackend){
    pool.addBackend(std::make_unique<BackendServer>("10.0.0.1", 8081));
    pool.addBackend(std::make_unique<BackendServer>("10.0.0.2", 8082));

    BackendServer* target = pool.getBackends()[0];

    bool removed = pool.removeBackend(target);

    EXPECT_TRUE(removed);
    EXPECT_EQ(pool.size(), 1);
    ASSERT_EQ(pool.getBackends().size(), 1);

    //confirm the surviving one is the correct one, not a leftover
    EXPECT_EQ(pool.getBackends()[0]->getHost(), "10.0.0.2");
}

//test 5: removing something newer added - failure path.
TEST_F(BackendPoolTest, RemoveNonExistentBackendFails){
    pool.addBackend(std::make_unique<BackendServer>("10.0.0.1", 8081));

    BackendServer phantom("10.0.0.99", 9999); //never added to the pool
    bool removed = pool.removeBackend(&phantom);

    EXPECT_FALSE(removed);
    EXPECT_EQ(pool.size(), 1); // unchanged
}

TEST_F(BackendPoolTest, RejectsNullWithoutChangingMembership) {
    pool.addBackend(std::make_unique<BackendServer>("localhost", 9001));
    BackendServer* original = pool.getBackends()[0];
    EXPECT_THROW(pool.addBackend(nullptr), std::invalid_argument);
    EXPECT_EQ(pool.size(), 1u);
    EXPECT_EQ(pool.getBackends()[0], original);
}

TEST_F(BackendPoolTest, GrowthKeepsAddressesAndRemovalKeepsOrder) {
    pool.addBackend(std::make_unique<BackendServer>("localhost", 9001));
    BackendServer* original = pool.getBackends()[0];
    for (int port = 9002; port < 9100; ++port) {
        pool.addBackend(std::make_unique<BackendServer>("localhost", port));
    }
    EXPECT_EQ(pool.getBackends()[0], original);
    BackendServer* removed = pool.getBackends()[1];
    BackendServer* survivor = pool.getBackends()[2];
    EXPECT_TRUE(pool.removeBackend(removed));
    EXPECT_EQ(pool.getBackends()[0], original);
    EXPECT_EQ(pool.getBackends()[1], survivor);
    EXPECT_FALSE(pool.removeBackend(nullptr));
}

