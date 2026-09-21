#include <doctest/doctest.h>

#include "src/render_graph/nodes/clear_policy.hpp"

using chronon3d::graph::detail::ClearExecutionPath;
using chronon3d::graph::detail::resolve_clear_execution_path;
using chronon3d::graph::detail::resolve_dirty_rect_clear_plan;

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

TEST_CASE("Clear dirty-rect policy classifies eligibility and clip cost") {
    const auto plan = resolve_dirty_rect_clear_plan(
        true, true, true, true, false,
        chronon3d::raster::BBox{0, 0, 40, 20}, 100, 100);
    CHECK(plan.use_dirty_rects);
    CHECK_FALSE(plan.empty_clip);
    CHECK_FALSE(plan.full_clip);
    CHECK(plan.clear_pixels == 800);
    CHECK(plan.clip_fraction == doctest::Approx(0.08f));
    CHECK_FALSE(plan.prefer_full_clear);
    CHECK_FALSE(plan.skip_clear);

    const auto large = resolve_dirty_rect_clear_plan(
        true, true, true, true, false,
        chronon3d::raster::BBox{0, 0, 80, 80}, 100, 100);
    CHECK(large.prefer_full_clear);

    const auto ineligible = resolve_dirty_rect_clear_plan(
        false, true, true, true, false, std::nullopt, 100, 100);
    CHECK_FALSE(ineligible.use_dirty_rects);
    CHECK(ineligible.full_clip);
    CHECK(ineligible.clear_pixels == 10000);
}
