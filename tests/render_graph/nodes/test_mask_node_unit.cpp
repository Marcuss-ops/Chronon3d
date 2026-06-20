// ==============================================================================
// tests/render_graph/nodes/test_mask_node_unit.cpp
//
// PR2 — MaskNode unit tests (3 tests, math/API correctness).
//
// MaskNode applies an alpha-mask multiplier on a layer's framebuffer.
// These unit tests verify the parts that DO NOT require a real framebuffer:
//   1. predicted_bbox() returns input unchanged (masks do not expand bounds).
//   2. cache_key changes when Mask geometry changes.
//   3. Modular coordinates flag participates in cache key (alpha rasterization
//      algorithm changes between "global"  and "local" coords → cache must).
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/nodes/mask_node.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>

#include <cmath>
#include <memory>
using namespace chronon3d;

static graph::RenderFrameInfo make_frame(int w, int h) {
    graph::RenderFrameInfo fi;
    fi.frame = Frame{0};
    fi.width = w;
    fi.height = h;
    return fi;
}

TEST_CASE("PR2-Unit-Mask: predicted_bbox is identity passthrough") {
    using chronon3d::graph::MaskNode;

    Mask m;
    m.type = MaskType::Circle;
    m.radius = 30.0f;
    m.pos = {10.0f, 20.0f, 0.0f};

    graph::RenderGraphContext ctx;
    ctx.frame = make_frame(200, 200);

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
    rect_big.size = {80.0f, 40.0f};   // larger

    auto node_rect = std::make_unique<MaskNode>(Mask{rect});
    auto node_circ = std::make_unique<MaskNode>(Mask{circ});
    auto node_big  = std::make_unique<MaskNode>(Mask{rect_big});

    graph::RenderGraphContext ctx;
    ctx.frame = make_frame(200, 200);
    ctx.options.modular_coordinates = false;

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
    ctx_a.frame = ctx_b.frame = make_frame(200, 200);
    ctx_a.options.modular_coordinates = false;
    ctx_b.options.modular_coordinates = true;

    CHECK(node->cache_key(ctx_a).params_hash != node->cache_key(ctx_b).params_hash);
}
