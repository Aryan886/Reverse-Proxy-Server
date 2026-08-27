// Temporary file. Confirms CMake + FetchContent(GoogleTest) + CTest
// are wired correctly before any real project code exists.
// Delete this once the first real Phase 1 test file is added.
#include <gtest/gtest.h>

TEST(Sanity, BuildPipelineWorks) {
    EXPECT_EQ(1 + 1, 2);
}
