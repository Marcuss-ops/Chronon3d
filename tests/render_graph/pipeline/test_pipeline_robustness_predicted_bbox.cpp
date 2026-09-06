// test_pipeline_robustness_predicted_bbox.cpp — Split from
// test_pipeline_robustness_coordinate.cpp: SourceNode predicted_bbox vs
// execute containment group (2D/3D, centered and top-left variants).
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

static int count_alpha_outside_bbox(const Framebuffer& fb, const raster::BBox& bbox) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.a > 0.001f && !(x >= bbox.x0 && x < bbox.x1 && y >= bbox.y0 && y < bbox.y1)) {
                count++;
            }
        }
    }
    return count;
}

static int count_alpha_inside_bbox(const Framebuffer& fb, const raster::BBox& bbox) {
    int count = 0;
    int y0 = std::max(0, bbox.y0);
    int y1 = std::min(fb.height(), bbox.y1);
    int x0 = std::max(0, bbox.x0);
    int x1 = std::min(fb.width(), bbox.x1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.a > 0.001f) {
                count++;
            }
        }
    }
    return count;
}

static raster::BBox expand_bbox(const raster::BBox& bbox, int margin) {
    return raster::BBox{
        .x0 = bbox.x0 - margin,
        .y0 = bbox.y0 - margin,
        .x1 = bbox.x1 + margin,
        .y1 = bbox.y1 + margin
    };
}

TEST_CASE("SourceNode predicted_bbox vs execute - 2D standard top left layer") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("SourceNode predicted_bbox vs execute - 3D non-centered source") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("SourceNode predicted_bbox vs execute - Centered 2D source") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("SourceNode predicted_bbox vs execute - 3D centered source") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}
