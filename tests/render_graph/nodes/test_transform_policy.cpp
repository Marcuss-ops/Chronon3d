#include <doctest/doctest.h>

#include "src/render_graph/nodes/transform_policy.hpp"

using chronon3d::graph::detail::TransformExecutionPath;
using chronon3d::graph::detail::resolve_transform_execution_path;

TEST_CASE("Transform policy selects native when all runtime resources exist") {
    CHECK(resolve_transform_execution_path(false, true, true, true) ==
          TransformExecutionPath::NativeSurface);
    CHECK(resolve_transform_execution_path(true, true, true, true) ==
          TransformExecutionPath::NativeSurface);
}

TEST_CASE("Transform policy falls back or rejects when native resources are missing") {
    CHECK(resolve_transform_execution_path(false, false, true, true) ==
          TransformExecutionPath::Software);
    CHECK(resolve_transform_execution_path(false, true, false, true) ==
          TransformExecutionPath::Software);
    CHECK(resolve_transform_execution_path(false, true, true, false) ==
          TransformExecutionPath::Software);
    CHECK(resolve_transform_execution_path(true, false, true, true) ==
          TransformExecutionPath::Reject);
    CHECK(resolve_transform_execution_path(true, true, false, true) ==
          TransformExecutionPath::Reject);
    CHECK(resolve_transform_execution_path(true, true, true, false) ==
          TransformExecutionPath::Reject);
}
