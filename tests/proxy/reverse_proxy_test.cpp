#include <gtest/gtest.h>
#include "reverse_proxy.h"
#include <stdexcept>

TEST(ReverseProxyTest, RejectsInvalidPortTimeoutsAndHeaderLimit) {
    ProxyConfig config;
    config.listenPort = 0;
    EXPECT_THROW(ReverseProxy{config}, std::invalid_argument);
    config.listenPort = 8080;
    config.ioTimeout = std::chrono::milliseconds(0);
    EXPECT_THROW(ReverseProxy{config}, std::invalid_argument);
    config.ioTimeout = std::chrono::milliseconds(1000);
    config.connectTimeout = std::chrono::milliseconds(-1);
    EXPECT_THROW(ReverseProxy{config}, std::invalid_argument);
    config.connectTimeout = std::chrono::milliseconds(1000);
    config.limits.maxHeaderBytes = 0;
    EXPECT_THROW(ReverseProxy{config}, std::invalid_argument);
}

TEST(ReverseProxyTest, RejectsDuplicateRoutesAndAllowsAnEmptyPool) {
    ProxyConfig config;
    config.routes.push_back({"/api", {}, false});
    EXPECT_NO_THROW(ReverseProxy{config});
    config.routes.push_back({"/api", {}, false});
    EXPECT_THROW(ReverseProxy{config}, std::invalid_argument);
}
