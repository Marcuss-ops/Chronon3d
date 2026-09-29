#include <doctest/doctest.h>
#include <tests/helpers/doctest_skip_compat.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp>
#include <cmath>
#include <array>
#include <cstdlib>
#include <string>
#include <mutex>
#include "src/render_graph/builder/graph_builder_coordinates.hpp"
#include "src/render_graph/builder/evaluated_layer_placement.hpp"
#include "src/render_graph/builder/graph_builder_internal.hpp"
#include "src/render_graph/executor/tile_pruning.hpp"
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;

TEST_CASE("EvaluatedLayerPlacement resolves canvas placement without changing it") {
    SceneBuilder builder;
    builder.layer("placement_canvas", [](LayerBuilder& layer) {
        layer.rect("placement_rect", {
            .size = {80.0f, 60.0f},
            .color = Color::red(),
            .pos = {20.0f, 30.0f, 0.0f},
        });
    });
    const Scene scene = builder.build();

    RenderGraphContext ctx;
    ctx.frame_input.width = 320;
    ctx.frame_input.height = 240;
    ctx.policy.modular_coordinates = true;
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene, ctx);
    REQUIRE(resolved.layers.size() == 1);

    const auto& layer = resolved.layers.front();
    LayerGraphItem item{
        .layer = layer.layer,
        .transform = layer.world_transform,
        .world_matrix = layer.world_matrix,
        .projected = false,
        .native_3d = false,
    };

    const auto placement = chronon3d::graph::detail::evaluate_layer_placement(item, ctx);
    CHECK(placement.space == chronon3d::graph::detail::EvaluatedCoordinateSpace::Canvas);
    CHECK(placement.visible);
    CHECK_FALSE(placement.requires_transform_node);
    CHECK_FALSE(placement.applies_camera_in_processor);
    CHECK_FALSE(placement.defer_camera_projection);
    CHECK(chronon3d::graph::detail::matrix_near(placement.world_matrix, item.world_matrix));
    CHECK(chronon3d::graph::detail::matrix_near(placement.source_matrix, item.world_matrix));
    CHECK(chronon3d::graph::detail::matrix_near(placement.render_matrix, item.world_matrix));
}

TEST_CASE("EvaluatedLayerPlacement resolves projected and native 3D ownership") {
    SceneBuilder builder;
    builder.layer("placement_3d", [](LayerBuilder& layer) {
        layer.enable_3d(true)
            .rect("placement_rect", {
                .size = {80.0f, 60.0f},
                .color = Color::blue(),
                .pos = {0.0f, 0.0f, 0.0f},
            });
    });
    const Scene scene = builder.build();
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene, RenderGraphContext{});
    REQUIRE(resolved.layers.size() == 1);

    RenderGraphContext projected_ctx;
    projected_ctx.frame_input.width = 320;
    projected_ctx.frame_input.height = 240;
    projected_ctx.frame_input.has_camera_2_5d = true;
    projected_ctx.policy.modular_coordinates = true;

    const auto& layer = resolved.layers.front();
    const Mat4 projection = glm::translate(Mat4(1.0f), Vec3(12.0f, 18.0f, 0.0f));

    LayerGraphItem projected_item{
        .layer = layer.layer,
        .transform = layer.world_transform,
        .world_matrix = layer.world_matrix,
        .projection_matrix = projection,
        .projected = true,
        .native_3d = false,
    };
    const auto projected = chronon3d::graph::detail::evaluate_layer_placement(projected_item, projected_ctx);
    CHECK(projected.space == chronon3d::graph::detail::EvaluatedCoordinateSpace::CameraProjected);
    CHECK(projected.visible);
    CHECK(projected.requires_transform_node);
    CHECK_FALSE(projected.applies_camera_in_processor);
    CHECK(projected.defer_camera_projection);
    CHECK(chronon3d::graph::detail::matrix_near(projected.source_matrix,
        chronon3d::graph::detail::implicit_canvas_center_matrix(projected_ctx)));
    const Mat4 expected_projected_matrix = projection * glm::scale(
        Mat4(1.0f), Vec3(1.0f, -1.0f, 1.0f));
    CHECK(chronon3d::graph::detail::matrix_near(
        projected.render_matrix, expected_projected_matrix));

    LayerGraphItem native_item{
        .layer = layer.layer,
        .transform = layer.world_transform,
        .world_matrix = layer.world_matrix,
        .projected = false,
        .native_3d = true,
    };
    const auto native = chronon3d::graph::detail::evaluate_layer_placement(native_item, projected_ctx);
    CHECK(native.space == chronon3d::graph::detail::EvaluatedCoordinateSpace::Native3D);
    CHECK(native.visible);
    CHECK_FALSE(native.requires_transform_node);
    CHECK(native.applies_camera_in_processor);
    CHECK_FALSE(native.defer_camera_projection);
    CHECK(chronon3d::graph::detail::matrix_near(native.source_matrix, layer.world_matrix));
    CHECK(chronon3d::graph::detail::matrix_near(native.render_matrix, layer.world_matrix));
}


TEST_CASE("evaluate_source_payload_placement keeps 2D layers still under a head-on 2.5D camera") {
    // Regression: a resolved layer matrix is expressed in canvas pixels
    // (origin top-left, y down, implicit canvas centre already baked in),
    // while the camera homography is authored in the canonical
    // centre-relative y-up basis and composes `canvas_center * ssaa * proj`
    // itself. Feeding the pixel matrix straight into
    // project_to_camera_space applied the centre twice and mirrored y: a 2D
    // path at canvas [480, 420] on 960x540 landed at (757.9, 26.8) instead
    // of (480.0, 183.1) for a perspective camera, and a shape at the centre
    // landed at (960, 0). The invariant below is the physical one: a layer
    // lying in the camera's z=0 plane is unaffected by a head-on camera at
    // unit scale (zoom == camera distance).
    // See camera_input_matrix() in graph_builder_coordinates.hpp.
    RenderGraphContext ctx;
    ctx.frame_input.width = 960;
    ctx.frame_input.height = 540;
    ctx.frame_input.camera_2_5d.enabled = true;
    ctx.frame_input.camera_2_5d.position = {0.0f, 0.0f, -800.0f};
    ctx.frame_input.camera_2_5d.zoom = 800.0f;
    ctx.frame_input.has_camera_2_5d = true;

    const Mat4 canvas_centre =
        chronon3d::graph::detail::implicit_canvas_center_matrix(ctx);
    REQUIRE(chronon3d::graph::detail::matrix_near(
        canvas_centre, glm::translate(Mat4(1.0f), Vec3(480.0f, 270.0f, 0.0f)), 1e-3f));

    // The conversion is the exact basis change canvas-pixel -> camera: a
    // translation-only pixel matrix must come back with the canvas centre
    // removed and the y axis pointing up, so canvas pixel (123, 456) becomes
    // camera position (123 - 480, 270 - 456).
    const Mat4 probe = glm::translate(Mat4(1.0f), Vec3(123.0f, 456.0f, 0.0f));
    const Mat4 probe_camera = chronon3d::graph::detail::camera_input_matrix(ctx, probe);
    CHECK(chronon3d::graph::detail::matrix_near(
        probe_camera,
        glm::translate(Mat4(1.0f), Vec3(-357.0f, -186.0f, 0.0f)) *
            glm::scale(Mat4(1.0f), Vec3(1.0f, -1.0f, 1.0f)),
        1e-3f));
    const Vec4 probe_origin = probe_camera * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(probe_origin.x == doctest::Approx(-357.0f).epsilon(1e-3));
    CHECK(probe_origin.y == doctest::Approx(-186.0f).epsilon(1e-3));

    const auto project = [&](const Vec3& canvas_position, bool defer = false) {
        return chronon3d::graph::detail::evaluate_source_payload_placement(
            glm::translate(Mat4(1.0f), canvas_position),
            1.0f,
            ctx,
            /*apply_camera_projection=*/true,
            /*defer_camera_projection=*/defer);
    };

    // A layer authored at the canvas centre sits on the camera axis, so the
    // projector must return the canvas centre itself, not centre + S*centre.
    const auto centred = project(Vec3(480.0f, 270.0f, 0.0f));
    REQUIRE(centred.has_value());
    CHECK(centred->space ==
          chronon3d::graph::detail::EvaluatedCoordinateSpace::CameraProjected);
    CHECK(centred->visible);
    CHECK(centred->requires_transform_node);
    CHECK(chronon3d::graph::detail::matrix_near(
        centred->render_matrix,
        glm::translate(Mat4(1.0f), Vec3(480.0f, 270.0f, 0.0f)), 1e-3f));

    // Anywhere else in the plane the layer must stay exactly where it was
    // authored: no doubled centre, no mirrored y, no scale drift.
    const std::array<Vec3, 4> authored_positions{
        Vec3(580.0f, 270.0f, 0.0f),
        Vec3(480.0f, 370.0f, 0.0f),
        Vec3(320.0f, 150.0f, 0.0f),
        Vec3(760.0f, 430.0f, 0.0f),
    };
    for (const auto& canvas_position : authored_positions) {
        CAPTURE(canvas_position);
        const auto placement = project(canvas_position);
        REQUIRE(placement.has_value());
        CHECK(chronon3d::graph::detail::matrix_near(
            placement->render_matrix,
            glm::translate(Mat4(1.0f), canvas_position), 1e-3f));
    }

    // The deferred (processor-owned) camera path must keep the untouched
    // pixel matrix: only the eager projection may rewrite it.
    const auto deferred = project(Vec3(480.0f, 420.0f, 0.0f), /*defer=*/true);
    REQUIRE(deferred.has_value());
    CHECK(deferred->defer_camera_projection);
    CHECK_FALSE(deferred->requires_transform_node);
    CHECK(chronon3d::graph::detail::matrix_near(
        deferred->render_matrix, glm::translate(Mat4(1.0f), Vec3(480.0f, 420.0f, 0.0f))));
}

TEST_CASE("EvaluatedLayerPlacement resolves hidden and projected visibility") {
    SceneBuilder builder;
    builder.layer("placement_hidden", [](LayerBuilder& layer) {
        layer.visible(false);
        layer.rect("hidden_rect", {
            .size = {40.0f, 40.0f},
            .color = Color::red(),
            .pos = {0.0f, 0.0f, 0.0f},
        });
    });
    const Scene scene = builder.build();
    RenderGraphContext ctx;
    ctx.frame_input.width = 320;
    ctx.frame_input.height = 240;
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene, ctx);
    REQUIRE(resolved.layers.size() == 1);
    const auto hidden = chronon3d::graph::detail::resolve_layer_graph_item(
        resolved.layers.front(), ctx);
    const auto hidden_placement =
        chronon3d::graph::detail::evaluate_layer_placement(hidden, ctx);
    CHECK_FALSE(hidden_placement.visible);

    SceneBuilder projected_builder;
    projected_builder.layer("placement_projected", [](LayerBuilder& layer) {
        layer.enable_3d(true).position({0.0f, 0.0f, 500.0f}).rect(
            "projected_rect", {
                .size = {40.0f, 40.0f},
                .color = Color::blue(),
                .pos = {0.0f, 0.0f, 0.0f},
            });
    });
    const Scene projected_scene = projected_builder.build();
    RenderGraphContext projected_ctx;
    projected_ctx.frame_input.width = 320;
    projected_ctx.frame_input.height = 240;
    projected_ctx.frame_input.camera_2_5d.enabled = true;
    projected_ctx.frame_input.has_camera_2_5d = true;
    projected_ctx.frame_input.camera_2_5d.position = {0.0f, 0.0f, -800.0f};
    projected_ctx.frame_input.camera_2_5d.zoom = 800.0f;
    const auto projected_resolved =
        chronon3d::graph::detail::resolve_layers(projected_scene, projected_ctx);
    REQUIRE(projected_resolved.layers.size() == 1);
    const auto projected_item =
        chronon3d::graph::detail::resolve_layer_graph_item(
            projected_resolved.layers.front(), projected_ctx);
    const auto projected_placement =
        chronon3d::graph::detail::evaluate_layer_placement(
            projected_item, projected_ctx);
    CHECK(projected_item.projected);
    CHECK(projected_placement.visible);
    CHECK(projected_placement.space ==
          chronon3d::graph::detail::EvaluatedCoordinateSpace::CameraProjected);
}
