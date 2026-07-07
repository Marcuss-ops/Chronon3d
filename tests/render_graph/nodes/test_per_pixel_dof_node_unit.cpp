// ==============================================================================
// tests/render_graph/nodes/test_per_pixel_dof_node_unit.cpp
//
// PR2 — PerPixelDofNode unit tests (3 tests, math/API correctness).
//
// PerPixelDofNode is the post-composite depth-of-field effect.  These unit
// tests verify the parts that DO NOT require a real framebuffer:
//   1. cache_key composition: focus_z / aperture / max_blur each vary the hash.
//   2. predicted_bbox expansion is symmetric around the input bbox.
//   3. compute_dof_blur_radius() (pure math) returns 0 when disabled.
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/nodes/per_pixel_dof_node.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>
using namespace chronon3d;
namespace ctt = chronon3d::test;

// Helper to build a Camera2_5DRuntime with arbitrary DoF settings.
static Camera2_5DRuntime make_cam2_5d(float focus_z, float aperture, float max_blur) {
    Camera2_5DRuntime cam;
    cam.dof.enabled   = true;
    cam.dof.focus_z   = focus_z;
    cam.dof.aperture  = aperture;
    cam.dof.max_blur  = max_blur;
    return cam;
}

TEST_CASE("PR2-Unit-DoF: cache_key varies with focus_z / aperture / max_blur") {
    using chronon3d::graph::PerPixelDofNode;

    auto ctx_a = graph::RenderGraphContext{};
    ctx_a.frame_input = ctt::make_render_frame_info(128, 128);

    auto node_a = PerPixelDofNode::create(make_cam2_5d(50.0f,  0.05f,   8.0f));
    auto node_b = PerPixelDofNode::create(make_cam2_5d(150.0f, 0.05f,   8.0f));    // focus_z differs
    auto node_c = PerPixelDofNode::create(make_cam2_5d(50.0f,  0.10f,   8.0f));   // aperture differs
    auto node_d = PerPixelDofNode::create(make_cam2_5d(50.0f,  0.05f,  16.0f));   // max_blur differs

    auto k_a = node_a->cache_key(ctx_a);
    auto k_b = node_b->cache_key(ctx_a);
    auto k_c = node_c->cache_key(ctx_a);
    auto k_d = node_d->cache_key(ctx_a);

    CHECK(k_a.scope == "per_pixel_dof");
    CHECK(k_a.params_hash != k_b.params_hash);
    CHECK(k_a.params_hash != k_c.params_hash);
    CHECK(k_a.params_hash != k_d.params_hash);
}

TEST_CASE("PR2-Unit-DoF: predicted_bbox expands symmetrically by max_blur") {
    using chronon3d::graph::PerPixelDofNode;

    auto node = PerPixelDofNode::create(make_cam2_5d(50.0f, 0.05f, 12.0f));
    graph::RenderGraphContext ctx;
    ctx.frame_input = ctt::make_render_frame_info(200, 200);

    raster::BBox input{50, 50, 100, 100};
    std::array<std::optional<raster::BBox>, 1> inputs = { input };
    auto out = node->predicted_bbox(ctx, inputs);

    REQUIRE(out.has_value());
    CHECK(out->x0 <= 50 - 12);
    CHECK(out->y0 <= 50 - 12);
    CHECK(out->x1 >= 100 + 12);
    CHECK(out->y1 >= 100 + 12);
}

TEST_CASE("PR2-Unit-DoF: compute_dof_blur_radius returns 0 when disabled") {
    DepthOfFieldSettings d;
    d.enabled = false;
    d.focus_z = 100.0f;
    d.aperture = 0.05f;
    d.max_blur = 16.0f;
    LensModel lens;
    lens.focal_length = 50.0f;
    lens.f_stop = 2.8f;
    lens.sensor_width = 36.0f;
    const float r = compute_dof_blur_radius(d, lens, /*layer_world_z=*/500.0f, 1920.0f);
    CHECK(r == doctest::Approx(0.0f));
}
