// tests/render_graph/executor/test_executor_levels_parallel.cpp
//
// Regression tests for the render-graph level parallel-dispatch predicate.
// Parallel execution must be enabled only when BOTH conditions hold:
//   1. the level contains more than one node, and
//   2. the scheduler can run more than one worker concurrently.

#include <doctest/doctest.h>

#include "src/render_graph/executor/executor_levels.hpp"

using chronon3d::graph::should_execute_level_in_parallel;

TEST_CASE("should_execute_level_in_parallel: true with >1 nodes and >1 concurrency") {
    CHECK(should_execute_level_in_parallel(2, 2) == true);
    CHECK(should_execute_level_in_parallel(2, 4) == true);
    CHECK(should_execute_level_in_parallel(5, 2) == true);
    CHECK(should_execute_level_in_parallel(5, 8) == true);
}

TEST_CASE("should_execute_level_in_parallel: false with only one node") {
    CHECK(should_execute_level_in_parallel(1, 2) == false);
    CHECK(should_execute_level_in_parallel(1, 4) == false);
    CHECK(should_execute_level_in_parallel(1, 1) == false);
}

TEST_CASE("should_execute_level_in_parallel: false with concurrency == 1") {
    CHECK(should_execute_level_in_parallel(2, 1) == false);
    CHECK(should_execute_level_in_parallel(5, 1) == false);
    CHECK(should_execute_level_in_parallel(1, 1) == false);
}

TEST_CASE("should_execute_level_in_parallel: false with zero nodes") {
    CHECK(should_execute_level_in_parallel(0, 2) == false);
    CHECK(should_execute_level_in_parallel(0, 1) == false);
    CHECK(should_execute_level_in_parallel(0, 0) == false);
}

TEST_CASE("should_execute_level_in_parallel: boundary values") {
    CHECK(should_execute_level_in_parallel(2, 2) == true);
    CHECK(should_execute_level_in_parallel(2, 1) == false);
    CHECK(should_execute_level_in_parallel(1, 2) == false);
}
