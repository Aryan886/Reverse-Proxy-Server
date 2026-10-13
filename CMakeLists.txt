cmake_minimum_required(VERSION 3.16)
project(ReverseProxy VERSION 0.1 LANGUAGES CXX)

# ---- Language standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Warnings (Section 16: clean compile with warnings enabled) ----
add_compile_options(-Wall -Wextra)

# ---- GoogleTest via FetchContent ----
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
# Windows/MSVC quirk setting; harmless on Linux, kept for portability.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ---- Subsystem library: backend (Phase 1) ----
add_library(backend
    src/backend/backend_server.cpp
    src/backend/backend_pool.cpp
)
target_include_directories(backend PUBLIC src/backend)

add_library(scheduling
    src/scheduling/round_robin_policy.cpp
    src/scheduling/least_connection_policy.cpp
)
target_include_directories(scheduling PUBLIC src/scheduling)
target_link_libraries(scheduling PUBLIC backend)

add_library(http src/http/http_parser.cpp src/http/header_rewriter.cpp)
target_include_directories(http PUBLIC src/http)

add_library(routing src/routing/path_router.cpp)
target_include_directories(routing PUBLIC src/routing)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(network src/network/socket.cpp)
    target_include_directories(network PUBLIC src/network)
    add_library(proxy src/proxy/reverse_proxy.cpp)
    target_include_directories(proxy PUBLIC src/proxy)
    target_link_libraries(proxy PUBLIC network http scheduling routing)
    add_executable(reverse_proxy src/main.cpp)
    target_link_libraries(reverse_proxy PRIVATE proxy)
else()
    message(STATUS "Linux proxy executable disabled; portable unit tests remain available")
endif()

# ---- Tests ----
add_subdirectory(tests)
