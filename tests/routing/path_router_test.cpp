#include <gtest/gtest.h>
#include "path_router.h"
#include "round_robin_policy.h"
#include <stdexcept>

TEST(PathRouterTest, MatchesPathsWithoutMixingSimilarPrefixes) {
    BackendPool api;
    RoundRobinPolicy policy;
    PathRouter router;
    router.addRoute("/api", api, policy);

    for (const char* target : {"/api", "/api/id", "/api/id?name=test"}) {
        const Route* route = router.findRoute(target);
        ASSERT_NE(route, nullptr);
        EXPECT_EQ(route->pool, &api);
        EXPECT_EQ(route->policy, &policy);
    }
    EXPECT_EQ(router.findRoute("/apix"), nullptr);
    EXPECT_EQ(router.findRoute("/unknown"), nullptr);
}

TEST(PathRouterTest, UsesTheFirstMatchingRoute) {
    BackendPool admin;
    BackendPool api;
    RoundRobinPolicy adminPolicy;
    RoundRobinPolicy apiPolicy;
    PathRouter router;
    router.addRoute("/api/admin", admin, adminPolicy);
    router.addRoute("/api", api, apiPolicy);
    router.addRoute("/api", admin, adminPolicy);

    ASSERT_NE(router.findRoute("/api/admin/users"), nullptr);
    EXPECT_EQ(router.findRoute("/api/admin/users")->pool, &admin);
    ASSERT_NE(router.findRoute("/api/users"), nullptr);
    EXPECT_EQ(router.findRoute("/api/users")->pool, &api);

    PathRouter broadFirst;
    broadFirst.addRoute("/api", api, apiPolicy);
    broadFirst.addRoute("/api/admin", admin, adminPolicy);
    ASSERT_NE(broadFirst.findRoute("/api/admin/users"), nullptr);
    EXPECT_EQ(broadFirst.findRoute("/api/admin/users")->pool, &api);
}

TEST(PathRouterTest, RejectsInvalidPrefixes) {
    BackendPool pool;
    RoundRobinPolicy policy;
    PathRouter router;
    EXPECT_EQ(router.findRoute("/api"), nullptr);
    EXPECT_THROW(router.addRoute("api", pool, policy), std::invalid_argument);
    EXPECT_THROW(router.addRoute("/api/", pool, policy), std::invalid_argument);
}

TEST(PathRouterTest, PoolsHaveIndependentPolicies) {
    BackendPool api;
    BackendPool assets;
    api.addBackend(BackendServer("127.0.0.1", 9001));
    api.addBackend(BackendServer("127.0.0.1", 9002));
    assets.addBackend(BackendServer("127.0.0.1", 9003));
    RoundRobinPolicy apiPolicy;
    RoundRobinPolicy assetPolicy;
    PathRouter router;
    router.addRoute("/api", api, apiPolicy);
    router.addRoute("/static", assets, assetPolicy);

    const Route* apiRoute = router.findRoute("/api/id");
    const Route* assetRoute = router.findRoute("/static/id");
    ASSERT_NE(apiRoute, nullptr);
    ASSERT_NE(assetRoute, nullptr);
    EXPECT_EQ(apiRoute->policy->selectBackend(*apiRoute->pool)->getPort(), 9001);
    EXPECT_EQ(assetRoute->policy->selectBackend(*assetRoute->pool)->getPort(), 9003);
    EXPECT_EQ(apiRoute->policy->selectBackend(*apiRoute->pool)->getPort(), 9002);
}
