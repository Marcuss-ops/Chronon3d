#include <doctest/doctest.h>

#include <string>

#include "src/render_graph/nodes/text_run/gpu_text_cached_policy.hpp"

using chronon3d::graph::text_run::detail::TextExecutionRoute;
using chronon3d::graph::text_run::detail::TextRouteReason;
using chronon3d::graph::text_run::detail::resolve_text_execution_route;

namespace {

constexpr bool kNativeSurfaces = true;
constexpr bool kNoNativeSurfaces = false;

} // namespace

TEST_CASE("Cached GPU text policy prioritizes explicit rejection gates") {
    const auto rejected =
        resolve_text_execution_route(true, kNativeSurfaces, false, true, true, true);
    CHECK(rejected.route == TextExecutionRoute::Reject);
    CHECK(rejected.reason == TextRouteReason::UnsupportedAnimation);

    // An unsupported animator stack is rejected before the lane is consulted:
    // the software hand-off must never mask a stack the GPU cannot express.
    const auto rejected_without_native_surfaces =
        resolve_text_execution_route(true, kNoNativeSurfaces, false, true, true, true);
    CHECK(rejected_without_native_surfaces.route == TextExecutionRoute::Reject);
    CHECK(rejected_without_native_surfaces.reason ==
          TextRouteReason::UnsupportedAnimation);

    const auto software =
        resolve_text_execution_route(false, kNativeSurfaces, true, true, true, true);
    CHECK(software.route == TextExecutionRoute::Software);
    CHECK(software.reason == TextRouteReason::ForcedSoftware);
}

TEST_CASE("Cached GPU text policy hands a non-native-surface lane to software") {
    // Every native text kernel needs a resident glyph surface.  A lane that
    // cannot host one has no GPU route, even with the prepared cache and the
    // surface registry present; it must not pick MTSDF and fail per frame.
    const auto decision =
        resolve_text_execution_route(false, kNoNativeSurfaces, false, true, true, true);
    CHECK(decision.route == TextExecutionRoute::Software);
    CHECK(decision.reason == TextRouteReason::NativeSurfacesUnavailable);
}

TEST_CASE("Cached GPU text policy selects MTSDF only with native resources") {
    CHECK(resolve_text_execution_route(false, kNativeSurfaces, false, true, true, true)
              .route == TextExecutionRoute::NativeMtsdf);
    CHECK(resolve_text_execution_route(false, kNativeSurfaces, false, false, true, true)
              .route == TextExecutionRoute::NativePacked);
    CHECK(resolve_text_execution_route(false, kNativeSurfaces, false, true, false, true)
              .route == TextExecutionRoute::NativePacked);
    CHECK(resolve_text_execution_route(false, kNativeSurfaces, false, true, true, false)
              .route == TextExecutionRoute::NativePacked);
}

TEST_CASE("Cached GPU text policy names every route and reason") {
    using chronon3d::graph::text_run::detail::text_execution_route_name;
    using chronon3d::graph::text_run::detail::text_route_reason_name;

    CHECK(std::string(text_execution_route_name(TextExecutionRoute::NativeMtsdf)) ==
          "native_mtsdf");
    CHECK(std::string(text_execution_route_name(TextExecutionRoute::NativePacked)) ==
          "native_packed");
    CHECK(std::string(text_execution_route_name(TextExecutionRoute::Software)) ==
          "software");
    CHECK(std::string(text_execution_route_name(TextExecutionRoute::Reject)) == "reject");
    CHECK(std::string(text_route_reason_name(
              TextRouteReason::NativeSurfacesUnavailable)) ==
          "native_surfaces_unavailable");
}
