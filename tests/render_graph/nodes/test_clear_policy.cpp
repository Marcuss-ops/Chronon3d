#include <doctest/doctest.h>

#include "src/render_graph/nodes/clear_policy.hpp"

using chronon3d::graph::detail::ClearExecutionPath;
using chronon3d::graph::detail::resolve_clear_execution_path;

TEST_CASE("Clear policy selects native only with request, backend and registry") {
    CHECK(resolve_clear_execution_path(false, true, true, true) ==
          ClearExecutionPath::NativeSurface);
    CHECK(resolve_clear_execution_path(true, false, true, true) ==
          ClearExecutionPath::NativeSurface);
    CHECK(resolve_clear_execution_path(false, false, true, true) ==
          ClearExecutionPath::Software);
    CHECK(resolve_clear_execution_path(true, true, false, true) ==
          ClearExecutionPath::Reject);
    CHECK(resolve_clear_execution_path(true, true, true, false) ==
          ClearExecutionPath::Reject);
    CHECK(resolve_clear_execution_path(true, false, true, true) ==
          ClearExecutionPath::NativeSurface);
}
