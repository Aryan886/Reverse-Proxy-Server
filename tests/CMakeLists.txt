include(GoogleTest)

# ---- Temporary sanity check ----
# Purpose: verify CMake + GoogleTest + CTest actually work together
# BEFORE any real class exists. Delete sanity_test.cpp once the first
# real Phase 1 test (e.g. test_round_robin_policy.cpp) is added.
add_executable(sanity_test sanity_test.cpp)
target_link_libraries(sanity_test PRIVATE GTest::gtest_main)
gtest_discover_tests(sanity_test)

# ---- Phase 1 tests ----
add_executable(test_backend_pool backend/backend_pool_test.cpp)
target_link_libraries(test_backend_pool PRIVATE backend GTest::gtest_main)
gtest_discover_tests(test_backend_pool)