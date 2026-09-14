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

# ---- Tests ----
add_subdirectory(tests)
