// ==============================================================================
// tests/render_graph/nodes/test_mask_node_unit.cpp
//
// PR2 — MaskNode unit tests (3 tests, math/API correctness).
//
// MaskNode applies an alpha-mask multiplier on a layer's framebuffer.
// These unit tests verify the parts that DO NOT require a real framebuffer:
//   1. predicted_bbox() returns input unchanged (masks do not expand bounds).
//   2. cache_key changes when Mask geometry changes.
//   3. Modular coordinates flag participates in cache key.
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/nodes/mask_node.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <memory>
using namespace chronon3d;
namespace ctt = chronon3d::test;

TEST_CASE("PR2-Unit-Mask: predicted_bbox is identity passthrough") {
    using chronon3d::graph::MaskNode;

    Mask m;
    m.type = MaskType::Circle;
    m.radius = 30.0f;
    m.pos = {10.0f, 20.0f, 0.0f};

    graph::RenderGraphContext ctx;
    ctx.frame_input = ctt::make_render_frame_info(200, 200);

    raster::BBox input{12, 18, 88, 92};
    std::array<std::optional<raster::BBox>, 1> inputs = { input };
    auto node = std::make_unique<MaskNode>(std::move(m));

    auto bbox = node->predicted_bbox(ctx, inputs);
    REQUIRE(bbox.has_value());
    CHECK(bbox->x0 == input.x0);
    CHECK(bbox->y0 == input.y0);
    CHECK(bbox->x1 == input.x1);
    CHECK(bbox->y1 == input.y1);
}

TEST_CASE("PR2-Unit-Mask: cache_key varies with mask type + radius") {
    using chronon3d::graph::MaskNode;

    Mask rect;
    rect.type = MaskType::Rect;
    rect.size = {40.0f, 20.0f};

    Mask circ;
    circ.type = MaskType::Circle;
    circ.radius = 30.0f;

    Mask rect_big;
    rect_big.type = MaskType::Rect;
    rect_big.size = {80.0f, 40.0f};

    auto node_rect = std::make_unique<MaskNode>(Mask{rect});
    auto node_circ = std::make_unique<MaskNode>(Mask{circ});
    auto node_big  = std::make_unique<MaskNode>(Mask{rect_big});

    graph::RenderGraphContext ctx;
    ctx.frame_input = ctt::make_render_frame_info(200, 200);
    ctx.policy.modular_coordinates = false;

    auto k_rect  = node_rect->cache_key(ctx);
    auto k_circ  = node_circ->cache_key(ctx);
    auto k_big   = node_big->cache_key(ctx);

    CHECK(k_rect.params_hash != k_circ.params_hash);
    CHECK(k_rect.params_hash != k_big.params_hash);
}

TEST_CASE("PR2-Unit-Mask: cache_key differs when modular_coordinates flag flips") {
    using chronon3d::graph::MaskNode;

    Mask m;
    m.type = MaskType::Rect;
    m.size = {50.0f, 50.0f};

    auto node = std::make_unique<MaskNode>(Mask{m});
    graph::RenderGraphContext ctx_a;
    graph::RenderGraphContext ctx_b;
    ctx_a.frame_input = ctt::make_render_frame_info(200, 200);
    ctx_b.frame_input = ctt::make_render_frame_info(200, 200);
    ctx_a.policy.modular_coordinates = false;
    ctx_b.policy.modular_coordinates = true;

    CHECK(node->cache_key(ctx_a).params_hash != node->cache_key(ctx_b).params_hash);
}

TEST_CASE("text mask source identity participates in the mask node cache key") {
    Mask first;
    first.type = MaskType::Text;
    first.text_source = "title_a";
    Mask second = first;
    second.text_source = "title_b";
    graph::MaskNode first_node(std::move(first), Frame{-1}, {"title_a"});
    graph::MaskNode second_node(std::move(second), Frame{-1}, {"title_b"});
    graph::RenderGraphContext ctx;
    ctx.frame_input = ctt::make_render_frame_info(64, 64);
    CHECK(first_node.cache_key(ctx).params_hash != second_node.cache_key(ctx).params_hash);
}

TEST_CASE("MaskNode applies same-frame text source alpha to the target framebuffer") {
    Mask mask;
    mask.type = MaskType::Text;
    mask.text_source = "label";
    Mat4 target_to_canvas{1.0f};
    target_to_canvas[3].x = 10.0f;
    target_to_canvas[3].y = 20.0f;
    graph::MaskNode node(std::move(mask), Frame{-1}, {"label"}, target_to_canvas,
                         graph::frame_variant_cache("text_mask_source"));

    Framebuffer target(4, 4);
    Framebuffer text(4, 4);
    target.clear(Color{1.0f, 0.5f, 0.25f, 1.0f});
    text.clear(Color::transparent());
    text.set_origin(10, 20);
    text.pixels_row(1)[1] = Color{1.0f, 1.0f, 1.0f, 0.6f};
    std::array<FramebufferRef, 2> inputs{FramebufferRef{target}, FramebufferRef{text}};
    std::array<std::optional<raster::BBox>, 2> bboxes{};

    graph::RenderGraphContext ctx;
    ctx.frame_input = ctt::make_render_frame_info(32, 32);
    static auto pool = cache::FramebufferPool::create_shared(1024 * 1024);
    ctx.framebuffer_pool = pool;
    ctx.policy.modular_coordinates = false;
    const auto output = node.execute(ctx, inputs, bboxes);
    REQUIRE(output);
    CHECK(output.value()->pixels_row(1)[1].a == doctest::Approx(0.6f));
    CHECK(output.value()->pixels_row(1)[1].r == doctest::Approx(0.6f));
    const auto first_frame_key = node.cache_key(ctx);
    ctx.frame_input.frame = Frame{1};
    CHECK(node.cache_key(ctx).frame != first_frame_key.frame);
}
