#include <doctest/doctest.h>

#include "src/render_graph/nodes/text_run/gpu_text_cached_policy.hpp"

using chronon3d::graph::text_run::detail::TextExecutionRoute;
using chronon3d::graph::text_run::detail::TextRouteReason;
using chronon3d::graph::text_run::detail::resolve_text_execution_route;

TEST_CASE("Cached GPU text policy prioritizes explicit rejection gates") {
    const auto rejected = resolve_text_execution_route(true, false, true, true, true);
    CHECK(rejected.route == TextExecutionRoute::Reject);
    CHECK(rejected.reason == TextRouteReason::UnsupportedAnimation);

    const auto software = resolve_text_execution_route(false, true, true, true, true);
    CHECK(software.route == TextExecutionRoute::Software);
    CHECK(software.reason == TextRouteReason::ForcedSoftware);
}

TEST_CASE("Cached GPU text policy selects MTSDF only with native resources") {
    CHECK(resolve_text_execution_route(false, false, true, true, true).route ==
          TextExecutionRoute::NativeMtsdf);
    CHECK(resolve_text_execution_route(false, false, false, true, true).route ==
          TextExecutionRoute::NativePacked);
    CHECK(resolve_text_execution_route(false, false, true, false, true).route ==
          TextExecutionRoute::NativePacked);
    CHECK(resolve_text_execution_route(false, false, true, true, false).route ==
          TextExecutionRoute::NativePacked);
}
