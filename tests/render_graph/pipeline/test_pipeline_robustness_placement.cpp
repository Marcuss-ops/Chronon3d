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
