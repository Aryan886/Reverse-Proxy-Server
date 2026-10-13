include(GoogleTest)

add_executable(test_backend_pool backend/backend_pool_test.cpp)
target_link_libraries(test_backend_pool PRIVATE backend GTest::gtest_main)
gtest_discover_tests(test_backend_pool)

add_executable(test_backend_server backend/backend_server_test.cpp)
target_link_libraries(test_backend_server PRIVATE backend GTest::gtest_main)
gtest_discover_tests(test_backend_server)

add_executable(test_scheduling scheduling/scheduling_policy_test.cpp)
target_link_libraries(test_scheduling PRIVATE scheduling GTest::gtest_main)
gtest_discover_tests(test_scheduling)

add_executable(test_http http/http_parser_test.cpp http/header_rewriter_test.cpp)
target_link_libraries(test_http PRIVATE http GTest::gtest_main)
gtest_discover_tests(test_http)

add_executable(test_routing routing/path_router_test.cpp)
target_link_libraries(test_routing PRIVATE routing scheduling GTest::gtest_main)
gtest_discover_tests(test_routing)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    add_executable(test_socket network/socket_test.cpp)
    target_link_libraries(test_socket PRIVATE network GTest::gtest_main Threads::Threads)
    gtest_discover_tests(test_socket)

    add_executable(test_proxy proxy/reverse_proxy_test.cpp)
    target_link_libraries(test_proxy PRIVATE proxy GTest::gtest_main)
    gtest_discover_tests(test_proxy)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME proxy_integration
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/proxy_integration_test.py
                --proxy $<TARGET_FILE:reverse_proxy>
                --backend ${PROJECT_SOURCE_DIR}/examples/backend_server.py)
        set_tests_properties(proxy_integration PROPERTIES TIMEOUT 45)
    else()
        message(STATUS "Python3 unavailable: HTTP integration test disabled")
    endif()
endif()
