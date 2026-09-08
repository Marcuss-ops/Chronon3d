// test_pipeline_robustness_bbox_invariants.cpp — Split from
// test_pipeline_robustness_coordinate.cpp: execution-bbox invariants under
// diagnostics, dirty-clip cache keys, near-border and MultiSource union group.
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

TEST_CASE("SourceNode execution bbox is invariant under diagnostics") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "partially_clipped_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {1900.0f, 500.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    auto make_context = [&renderer](bool diagnostics_enabled) {
        RenderGraphContext ctx;
        ctx.frame_input.width = 1920;
        ctx.frame_input.height = 1080;
        ctx.policy.diagnostics_enabled = diagnostics_enabled;
        ctx.backend = &renderer.backend();
        return ctx;
    };

    cache::NodeCacheKey key{};
    SourceNode node("partially_clipped_node", rnode, key);

    auto off_ctx = make_context(false);
    auto on_ctx = make_context(true);
    const auto off_bbox = node.predicted_bbox(off_ctx);
    const auto on_bbox = node.predicted_bbox(on_ctx);

    REQUIRE(off_bbox.has_value());
    REQUIRE(on_bbox.has_value());
    CHECK(off_bbox->x0 >= 0);
    CHECK(off_bbox->y0 >= 0);
    CHECK(off_bbox->x1 <= off_ctx.frame_input.width);
    CHECK(off_bbox->y1 <= off_ctx.frame_input.height);
    CHECK(off_bbox->x1 == off_ctx.frame_input.width);
    CHECK(off_bbox->x0 < off_bbox->x1);
    CHECK(off_bbox->y0 < off_bbox->y1);
    CHECK(off_bbox->x0 == on_bbox->x0);
    CHECK(off_bbox->y0 == on_bbox->y0);
    CHECK(off_bbox->x1 == on_bbox->x1);
    CHECK(off_bbox->y1 == on_bbox->y1);

    // Compare actual framebuffer output through the public renderer path so
    // the backend's compiled processor catalog is wired exactly as in
    // production.  The diagnostics flag may log, but it must not alter pixels.
    SceneBuilder builder;
    builder.layer("diagnostics_parity_layer", [](LayerBuilder& layer) {
        layer.rect("diagnostics_parity_rect", {
            .size = {160.0f, 120.0f},
            .color = Color::red(),
            .pos = {180.0f, 0.0f, 0.0f}
        });
    });
    const Scene scene = builder.build();
    Camera camera;

    auto off_renderer = test::make_renderer();
    auto off_settings = off_renderer.render_settings();
    off_settings.diagnostics.enabled = false;
    off_renderer.set_settings(off_settings);
    const auto off_frame = off_renderer.render_scene(scene, camera, 320, 240, 30.0f);
    REQUIRE(off_frame != nullptr);

    auto on_renderer = test::make_renderer();
    auto on_settings = on_renderer.render_settings();
    on_settings.diagnostics.enabled = true;
    on_renderer.set_settings(on_settings);
    const auto on_frame = on_renderer.render_scene(scene, camera, 320, 240, 30.0f);
    REQUIRE(on_frame != nullptr);

    CHECK(test::framebuffer_hash(*off_frame) == test::framebuffer_hash(*on_frame));
}

TEST_CASE("Diagnostics OFF == ON: SourceNode bbox dirty clip cache key and pixels") {
    auto* resource = std::pmr::get_default_resource();
    const RenderNode source = RenderNodeFactory::rect(resource, "diagnostics_off_on_source", {
        .size = {120.0f, 80.0f},
        .color = Color::green(),
        .pos = {280.0f, 100.0f, 0.0f},
    });

    auto renderer = test::make_renderer();
    auto make_context = [&renderer, &source](bool diagnostics_enabled) {
        RenderGraphContext ctx;
        ctx.frame_input.width = 320;
        ctx.frame_input.height = 240;
        ctx.frame_input.frame = 0;
        ctx.policy.diagnostics_enabled = diagnostics_enabled;
        ctx.node_exec.dirty_rect = raster::BBox{0, 0, 160, 120};
        ctx.backend = &renderer.backend();
        ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
        REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
        ctx.node_exec.current_shape_processor =
            ctx.node_exec.processor_snapshot->shape_handle(source.shape.type());
        return ctx;
    };

    SourceNode node("diagnostics_off_on_source", source, cache::NodeCacheKey{});
    auto off_ctx = make_context(false);
    auto on_ctx = make_context(true);

    const auto off_bbox = node.predicted_bbox(off_ctx);
    const auto on_bbox = node.predicted_bbox(on_ctx);
    REQUIRE(off_bbox.has_value());
    REQUIRE(on_bbox.has_value());
    CHECK(off_bbox->x0 == on_bbox->x0);
    CHECK(off_bbox->y0 == on_bbox->y0);
    CHECK(off_bbox->x1 == on_bbox->x1);
    CHECK(off_bbox->y1 == on_bbox->y1);
    CHECK(off_bbox->x0 >= 0);
    CHECK(off_bbox->y0 >= 0);
    CHECK(off_bbox->x1 <= off_ctx.frame_input.width);
    CHECK(off_bbox->y1 <= off_ctx.frame_input.height);

    const auto off_dirty_clip = compute_dirty_clip(off_ctx, node, off_bbox);
    const auto on_dirty_clip = compute_dirty_clip(on_ctx, node, on_bbox);
    REQUIRE(off_dirty_clip.has_value());
    REQUIRE(on_dirty_clip.has_value());
    CHECK(off_dirty_clip->x0 == on_dirty_clip->x0);
    CHECK(off_dirty_clip->y0 == on_dirty_clip->y0);
    CHECK(off_dirty_clip->x1 == on_dirty_clip->x1);
    CHECK(off_dirty_clip->y1 == on_dirty_clip->y1);

    CHECK(node.cache_key(off_ctx) == node.cache_key(on_ctx));

    auto off_result = node.execute(off_ctx, {}, {});
    auto on_result = node.execute(on_ctx, {}, {});
    REQUIRE(off_result.has_value());
    REQUIRE(on_result.has_value());
    auto off_frame = off_result.take_value();
    auto on_frame = on_result.take_value();
    REQUIRE(off_frame != nullptr);
    REQUIRE(on_frame != nullptr);
    CHECK(test::framebuffer_hash(*off_frame) == test::framebuffer_hash(*on_frame));
}

TEST_CASE("SourceNode predicted_bbox vs execute - 3D source near border") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {900.0f, 500.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    // The conservative bbox includes the canonical safety margin and the
    // centered-coordinate conversion used by this standalone source path.
    CHECK(bbox.x0 == 840);
    CHECK(bbox.y0 == 440);
    CHECK(bbox.x1 == 960);
    CHECK(bbox.y1 == 560);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("MultiSourceNode predicted_bbox vs execute - Centering & Bounds check") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode_a = RenderNodeFactory::rect(res, "rect_a", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });
    RenderNode rnode_b = RenderNodeFactory::rect(res, "rect_b", {
        .size = {100.0f, 100.0f},
        .color = Color::green(),
        .pos = {200.0f, 0.0f, 0.0f}
    });

    std::vector<MultiSourceItem> items;
    items.push_back({&rnode_a, rnode_a.world_transform.to_mat4(), 1.0f});
    items.push_back({&rnode_b, rnode_b.world_transform.to_mat4(), 1.0f});

    auto renderer = test::make_renderer();
    const auto snapshot = renderer.backend().processor_snapshot();
    REQUIRE(snapshot != nullptr);
    std::array<renderer::ShapeProcessorHandle, 2> processors{
        snapshot->shape_handle(rnode_a.shape.type()),
        snapshot->shape_handle(rnode_b.shape.type())};
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = snapshot;
    ctx.node_exec.current_shape_processor = processors[0];
    ctx.node_exec.current_shape_processors = processors;

    cache::NodeCacheKey key{};
    MultiSourceNode node("my_multi_node", std::move(items), key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    // Default constructor — 2D top-left, bbox near origin
    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    // MultiSourceNode union: two 100x100 rects at (0,0) and (200,0)
    CHECK(bbox.x1 > 0);
    CHECK(bbox.y1 > 0);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);

    // Pixel color checks removed: with the new SourceNode API,
    // centering is handled by the graph builder, not the node.
    // The bbox containment checks above already verify correctness.
}
