#include <gtest/gtest.h>
#include "path_router.h"
#include "backend_pool.h"
#include "round_robin_policy.h"
#include <stdexcept>

TEST(PathRouterTest, UsesSegmentBoundariesAndIgnoresQuery) {
    BackendPool api, assets;
    RoundRobinPolicy apiPolicy, assetPolicy;
    PathRouter router;
    router.addRoute("/api", api, apiPolicy);
    router.addRoute("/static", assets, assetPolicy);
    for (const char* target : {"/api", "/api/users", "/api?name=test"}) {
        const Route* route = router.findRoute(target);
        ASSERT_NE(route, nullptr);
        EXPECT_EQ(route->pool, &api);
        EXPECT_EQ(route->policy, &apiPolicy);
    }
    EXPECT_EQ(router.findRoute("/apix"), nullptr);
    EXPECT_EQ(router.findRoute("/API"), nullptr);
    EXPECT_EQ(router.findRoute("/api%2Fusers"), nullptr);
    EXPECT_EQ(router.findRoute("/unknown"), nullptr);
    ASSERT_NE(router.findRoute("/static/item"), nullptr);
    EXPECT_EQ(router.findRoute("/static/item")->pool, &assets);
}

TEST(PathRouterTest, SelectsLongestPrefixRegardlessOfInsertionOrder) {
    BackendPool root, api, admin;
    RoundRobinPolicy rootPolicy, apiPolicy, adminPolicy;
    PathRouter router;
    router.addRoute("/api/admin", admin, adminPolicy);
    router.addRoute("/", root, rootPolicy);
    router.addRoute("/api", api, apiPolicy);
    ASSERT_NE(router.findRoute("/api/admin/users?sort=id"), nullptr);
    EXPECT_EQ(router.findRoute("/api/admin/users?sort=id")->pool, &admin);
    EXPECT_EQ(router.findRoute("/api/administration")->pool, &api);
    EXPECT_EQ(router.findRoute("/other")->pool, &root);
}

TEST(PathRouterTest, EmptyRouterAndInvalidConfiguration) {
    BackendPool pool;
    RoundRobinPolicy policy;
    PathRouter router;
    EXPECT_EQ(router.findRoute("/api"), nullptr);
    for (const char* prefix : {"", "api", "/api/", "/api?x=1", "/api#x", "/api x"}) {
        EXPECT_THROW(router.addRoute(prefix, pool, policy), std::invalid_argument);
    }
    router.addRoute("/api", pool, policy);
    EXPECT_THROW(router.addRoute("/api", pool, policy), std::invalid_argument);
}

TEST(PathRouterTest, PoolPoliciesKeepIndependentCursors) {
    BackendPool api, assets;
    api.addBackend(std::make_unique<BackendServer>("localhost", 9001));
    api.addBackend(std::make_unique<BackendServer>("localhost", 9002));
    assets.addBackend(std::make_unique<BackendServer>("localhost", 9003));
    assets.addBackend(std::make_unique<BackendServer>("localhost", 9004));
    RoundRobinPolicy apiPolicy, assetPolicy;
    PathRouter router;
    router.addRoute("/api", api, apiPolicy);
    router.addRoute("/static", assets, assetPolicy);
    auto apiRoute = router.findRoute("/api/id");
    auto assetRoute = router.findRoute("/static/id");
    EXPECT_EQ(apiRoute->policy->selectBackend(apiRoute->pool->getBackends())->getPort(), 9001);
    EXPECT_EQ(assetRoute->policy->selectBackend(assetRoute->pool->getBackends())->getPort(), 9003);
    EXPECT_EQ(apiRoute->policy->selectBackend(apiRoute->pool->getBackends())->getPort(), 9002);
    EXPECT_EQ(assetRoute->policy->selectBackend(assetRoute->pool->getBackends())->getPort(), 9004);
}
