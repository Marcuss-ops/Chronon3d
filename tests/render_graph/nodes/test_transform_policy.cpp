#include <doctest/doctest.h>

#include <chronon3d/math/transform.hpp>
#include "src/render_graph/nodes/transform_policy.hpp"

using chronon3d::graph::detail::TransformExecutionPath;
using chronon3d::graph::detail::resolve_transform_execution_path;
using chronon3d::graph::detail::clip_transform_bounds;
using chronon3d::graph::detail::normalize_transform_source_rect;

TEST_CASE("RenderState combine preserves resolved camera projection for child nodes") {
    chronon3d::RenderState parent;
    parent.projection.ready = true;
    parent.projection.focal = 73.0f;
    parent.projection.view[3][0] = 4.0f;
    parent.world_matrix[3][1] = 9.0f;
    parent.uses_2_5d_projection = true;

    chronon3d::Transform child;
    child.position = {2.0f, 3.0f, 4.0f};
    const auto combined = chronon3d::combine(parent, child);

    CHECK(combined.projection.ready);
    CHECK(combined.projection.focal == doctest::Approx(73.0f));
    CHECK(combined.projection.view[3][0] == doctest::Approx(4.0f));
    CHECK(combined.world_matrix[3][1] == doctest::Approx(9.0f));
    CHECK(combined.uses_2_5d_projection);
}

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

TEST_CASE("Transform geometry policy normalizes source bbox in framebuffer coordinates") {
    const auto rect = normalize_transform_source_rect(
        chronon3d::raster::BBox{95, 198, 140, 250}, 100, 200, 32, 40);
    CHECK(rect.x0 == 0.0f);
    CHECK(rect.y0 == 0.0f);
    CHECK(rect.x1 == 32.0f);
    CHECK(rect.y1 == 40.0f);
    CHECK(rect.valid());

    const auto empty = normalize_transform_source_rect(
        chronon3d::raster::BBox{0, 0, 10, 10}, 100, 100, 32, 32);
    CHECK_FALSE(empty.valid());
}

TEST_CASE("Transform geometry policy clips projected bounds and rejects empty intersections") {
    const auto clipped = clip_transform_bounds(
        -4.2f, 2.1f, 12.4f, 20.8f, 0, 0, 16, 16,
        chronon3d::raster::BBox{4, 4, 10, 12});
    REQUIRE(clipped.has_value());
    CHECK(clipped->x0 == 4);
    CHECK(clipped->y0 == 4);
    CHECK(clipped->x1 == 10);
    CHECK(clipped->y1 == 12);

    CHECK_FALSE(clip_transform_bounds(
        0.0f, 0.0f, 2.0f, 2.0f, 0, 0, 16, 16,
        chronon3d::raster::BBox{4, 4, 8, 8}).has_value());
}
