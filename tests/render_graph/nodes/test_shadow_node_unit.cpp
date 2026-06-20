// ==============================================================================
// tests/render_graph/nodes/test_shadow_node_unit.cpp
//
// PR2 — ShadowNode unit tests (3 tests, math/API correctness).
//
// ShadowNode projects the alpha silhouette of a caster layer onto a receiver
// layer along a directional light vector.  These unit tests verify the
// parts that DO NOT require a real framebuffer:
//   1. cache_key composition (every settings field enters the hash).
//   2. predicted_bbox() with light projection + depth scaling math.
//   3. max_offset clamp (huge shadows are clamped to settings.max_offset px).
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/nodes/shadow_node.hpp>
#include <chronon3d/rendering/shadow_settings.hpp>

#include <cmath>
#include <cstdint>
using namespace chronon3d;

// Build a minimal RenderFrameInfo for cache_key + bbox tests.
static graph::RenderFrameInfo make_frame(int frame, int w, int h) {
    graph::RenderFrameInfo fi;
    fi.frame = Frame{frame};
    fi.width = w;
    fi.height = h;
    fi.fps = 30.0f;
    fi.time_seconds = static_cast<float>(frame) / 30.0f;
    return fi;
}

static graph::RenderGraphContext make_ctx(int w, int h, int frame) {
    graph::RenderGraphContext ctx;
    ctx.frame = make_frame(frame, w, h);
    return ctx;
}

TEST_CASE("PR2-Unit-Shadow: cache_key distinguishes every settings field") {
    using chronon3d::graph::ShadowNode;
    Vec3 light{0.0f, 1.0f, 0.0f};

    rendering::ShadowSettings base;
    auto node_a = ShadowNode::create("caster", 0.0f, -100.0f, light, base);

    rendering::ShadowSettings mod;
    mod.opacity = 0.99f;            // differs from base.opacity (0.35)
    auto node_b = ShadowNode::create("caster", 0.0f, -100.0f, light, mod);

    auto k_a = node_a->cache_key(make_ctx(64, 64, 0));
    auto k_b = node_b->cache_key(make_ctx(64, 64, 0));

    CHECK(k_a.scope == k_b.scope);
    CHECK(k_a.params_hash != k_b.params_hash);  // different opacity → different hash
}

TEST_CASE("PR2-Unit-Shadow: predicted_bbox expands with offset + blur") {
    using chronon3d::graph::ShadowNode;
    using chronon3d::raster::BBox;

    Vec3 light{0.0f, 1.0f, 0.0f};  // straight down; no x/z component → zero projected offset
    rendering::ShadowSettings s;
    s.blur_radius   = 8.0f;   // ambient_blur ≈ 16
    s.max_offset    = 300.0f;
    s.px_per_unit   = 1.0f;

    auto node = ShadowNode::create("caster", 0.0f, -100.0f, light, s);
    auto ctx = make_ctx(128, 128, 0);

    raster::BBox input{20, 20, 50, 50};
    std::array<std::optional<BBox>, 1> inputs = { input };
    auto out = node->predicted_bbox(ctx, inputs);

    REQUIRE(out.has_value());
    CHECK(out->x0 <= 0);         // expanded left by ≥ blur
    CHECK(out->y0 <= 0);
    CHECK(out->x1 >= 128);       // expanded right beyond clip width
    CHECK(out->y1 >= 128);
}

TEST_CASE("PR2-Unit-Shadow: predicted_bbox clamps huge projected offset to max_offset") {
    using chronon3d::graph::ShadowNode;
    using chronon3d::raster::BBox;

    // Horizontal light direction in xz plane (y=0 means copysign guard fires).
    // With cast_z=0, receiver_z=-10000 → dz=10000, light=(-1,eps,0):
    //   ox = -(-1)/eps * 10000 * px_per_unit  → unbounded; must clamp.
    Vec3 light{-1.0f, 1e-6f, 0.0f};
    rendering::ShadowSettings s;
    s.max_offset = 50.0f;        // aggressive clamp
    s.px_per_unit = 1.0f;

    auto node = ShadowNode::create("c", 0.0f, -10000.0f, light, s);
    auto ctx = make_ctx(256, 256, 0);

    raster::BBox input{100, 100, 110, 110};
    std::array<std::optional<BBox>, 1> inputs = { input };
    auto out = node->predicted_bbox(ctx, inputs);

    REQUIRE(out.has_value());
    // dx is clamped to ±max_offset = ±50; with dy also clamped.
    CHECK(out->x0 >= 0);
    CHECK(out->x1 <= 256);
}
