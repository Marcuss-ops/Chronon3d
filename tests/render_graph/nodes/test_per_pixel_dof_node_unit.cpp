// ==============================================================================
// tests/render_graph/nodes/test_per_pixel_dof_node_unit.cpp
//
// PR2 — PerPixelDofNode unit tests (3 tests, math/API correctness).
//
// PerPixelDofNode is the post-composite depth-of-field effect.  These unit
// tests verify the parts that DO NOT require a real framebuffer:
//   1. cache_key composition: focus distance / f-stop / max blur vary the hash.
//   2. predicted_bbox expansion is symmetric around the input bbox.
//   3. compute_dof_blur_radius() (pure math) returns 0 when disabled.
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/nodes/per_pixel_dof_node.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <src/render_graph/nodes/composite_depth_mask.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
using namespace chronon3d;
namespace ctt = chronon3d::test;

// Helper to build a Camera2_5DRuntime with arbitrary DoF settings.
static Camera2_5DRuntime make_cam2_5d(float focus_distance, float f_stop, float max_blur) {
    Camera2_5DRuntime cam;
    cam.dof.enabled   = true;
    cam.dof.focus_distance = focus_distance;
    cam.lens.f_stop = f_stop;
    cam.dof.max_blur  = max_blur;
    return cam;
}

TEST_CASE("PR2-Unit-DoF: authored iris shaping fails closed, highlight weighting is consumed") {
    DepthOfFieldSettings foundational;
    foundational.enabled = true;
    CHECK_NOTHROW(validate_dof_render_capabilities(foundational));

    DepthOfFieldSettings iris = foundational;
    iris.iris_blades = 6;
    CHECK_THROWS_AS(validate_dof_render_capabilities(iris), std::runtime_error);

    // Highlight weighting exists in both kernels (dof_blur.comp
    // `highlight_weight`, software `dof_highlight_weight`), so an authored
    // value must reach the kernels rather than be rejected as unimplemented.
    DepthOfFieldSettings highlight = foundational;
    highlight.highlight_gain = 1.5f;
    CHECK_NOTHROW(validate_dof_render_capabilities(highlight));

    DepthOfFieldSettings both = foundational;
    both.iris_blades = 6;
    both.highlight_gain = 1.5f;
    CHECK_THROWS_AS(validate_dof_render_capabilities(both), std::runtime_error);

    DepthOfFieldSettings unsupported_gain = foundational;
    unsupported_gain.highlight_gain = 8.01f;
    CHECK_THROWS_AS(validate_dof_render_capabilities(unsupported_gain), std::runtime_error);
    unsupported_gain.highlight_gain = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(validate_dof_render_capabilities(unsupported_gain), std::runtime_error);
}

TEST_CASE("DOF rejects missing tracked depth but permits untracked passthrough") {
    auto camera = make_cam2_5d(50.f, 2.8f, 8.f);
    auto node = graph::PerPixelDofNode::create(camera);
    graph::RenderGraphContext ctx;
    ctx.frame_input = ctt::make_render_frame_info(4, 4);
    ctx.framebuffer_pool = cache::FramebufferPool::create_shared(1024 * 1024);
    Framebuffer input(4, 4);
    input.clear(Color{1.f, 0.f, 0.f, 1.f});
    std::array<FramebufferRef, 1> inputs{FramebufferRef{input}};
    std::array<std::optional<raster::BBox>, 1> bounds{};

    ctx.policy.track_dof_depth = true;
    CHECK_FALSE(node->execute(ctx, inputs, bounds));
    ctx.node_exec.dof_depth.assign(15, 1.f);
    CHECK_FALSE(node->execute(ctx, inputs, bounds));
    ctx.node_exec.dof_depth.clear();
    ctx.policy.track_dof_depth = false;
    const auto passthrough = node->execute(ctx, inputs, bounds);
    REQUIRE(passthrough);
    CHECK(passthrough.value()->get_pixel(0, 0).r == doctest::Approx(1.f));
}

TEST_CASE("DOF rejects incomplete native depth provenance") {
    auto node = graph::PerPixelDofNode::create(make_cam2_5d(50.f, 2.8f, 8.f));
    graph::RenderGraphContext ctx;
    ctx.frame_input = ctt::make_render_frame_info(4, 4);
    ctx.framebuffer_pool = cache::FramebufferPool::create_shared(1024 * 1024);
    ctx.policy.track_dof_depth = true;
    ctx.policy.require_native_gpu = true;
    Framebuffer input(4, 4);
    input.clear(Color{1.f, 0.f, 0.f, 1.f});
    std::array<FramebufferRef, 1> inputs{FramebufferRef{input}};
    std::array<std::optional<raster::BBox>, 1> bounds{};

    // A partial/mixed native depth publication must fail before any blur or
    // sharp pass-through; this requires no live GPU or backend mock.
    ctx.node_exec.native_dof_depth.surface = std::make_shared<Framebuffer>(4, 4);
    ctx.node_exec.native_dof_depth.unusable = true;
    auto result = node->execute(ctx, inputs, bounds);
    REQUIRE_FALSE(result);
    CHECK(result.error().message == "DOF depth is only partially device-local");
    CHECK_FALSE(ctx.node_exec.native_dof_depth.surface);

    // Failed native publication can leave the allocated CPU depth buffer at
    // the correct size. It is not a complete native depth surface and must
    // not be mistaken for a valid strict-native fallback.
    ctx.node_exec.dof_depth.assign(16, 1.f);
    ctx.node_exec.native_dof_depth.unusable = true;
    result = node->execute(ctx, inputs, bounds);
    REQUIRE_FALSE(result);
    CHECK(result.error().message == "DOF depth is only partially device-local");
    ctx.node_exec.dof_depth.clear();

    // Once an out-of-focus source is expected, missing device-local depth
    // must fail closed instead of silently returning a sharp frame.
    ctx.node_exec.native_dof_depth.unusable = false;
    ctx.node_exec.native_dof_state().unusable = false;
    ctx.node_exec.dof_depth.assign(16, kUnsetDofDepth);
    ctx.node_exec.dof_source_coverage.source_bbox = raster::BBox{0, 0, 4, 4};
    ctx.node_exec.dof_source_coverage.max_radius = 2.f;
    result = node->execute(ctx, inputs, bounds);
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find(
        "DOF depth tracking requires device-local depth on the native GPU path") == 0);

    // Even CPU depth without a native image cannot satisfy an expected visible
    // blur source on the strict GPU path.
    for (float depth : {0.f, 50.f}) {
        ctx.node_exec.dof_depth.assign(16, depth);
        result = node->execute(ctx, inputs, bounds);
        REQUIRE_FALSE(result);
        CHECK(result.error().message.find(
            "DOF depth tracking requires device-local depth on the native GPU path") == 0);
        CHECK(result.error().message.find("native_depth_available=false") != std::string::npos);
    }
}

TEST_CASE("Camera composite publishes resolved per-pixel depth to DOF") {
    for (bool mesh_first : {false, true}) {
        graph::RenderGraphContext ctx;
        ctx.frame_input = ctt::make_render_frame_info(2, 1);
        ctx.frame_input.has_camera_2_5d = true;
        ctx.frame_input.camera_2_5d.enabled = true;
        ctx.policy.track_dof_depth = true;
        ctx.node_exec.dof_depth.assign(2, kUnsetDofDepth);
        ctx.node_exec.dof_source_coverage.source_bbox = raster::BBox{0, 0, 1, 1};
        ctx.node_exec.dof_source_coverage.max_radius = 4.f;
        Framebuffer base(2, 1), mesh(2, 1), plane(2, 1), first(2, 1), output(2, 1);
        base.clear(Color::transparent());
        mesh.clear(Color{0.5f, 0.f, 0.f, 0.5f});
        mesh.set_depth_map({50.f, 110.f});
        plane.clear(Color{0.f, 0.f, 1.f, 1.f});
        plane.set_depth_map({80.f, 80.f});
        const Framebuffer& a = mesh_first ? mesh : plane;
        const Framebuffer& b = mesh_first ? plane : mesh;
        REQUIRE(graph::recompose_camera_composite(ctx, base, a, first, false, {}, 0.f));
        REQUIRE(graph::recompose_camera_composite(ctx, first, b, output, false, {}, 0.f));
        CHECK(ctx.node_exec.dof_depth[0] == doctest::Approx(50.f));
        CHECK(ctx.node_exec.dof_depth[1] == doctest::Approx(80.f));
        CHECK(output.get_pixel(0, 0).r == doctest::Approx(0.5f));
        CHECK(output.get_pixel(0, 0).b == doctest::Approx(0.5f));
        CHECK(output.get_pixel(1, 0).r == doctest::Approx(0.f));
        CHECK(output.get_pixel(1, 0).b == doctest::Approx(1.f));
        CHECK_FALSE(ctx.node_exec.dof_source_coverage.source_bbox.has_value());
    }
}

TEST_CASE("PR2-Unit-DoF: cache_key varies with optics, blur limits and shaping") {
    using chronon3d::graph::PerPixelDofNode;

    auto ctx_a = graph::RenderGraphContext{};
    ctx_a.frame_input = ctt::make_render_frame_info(128, 128);

    auto node_a = PerPixelDofNode::create(make_cam2_5d(50.0f, 2.8f,  8.0f));
    auto node_b = PerPixelDofNode::create(make_cam2_5d(150.0f, 2.8f, 8.0f));
    auto node_c = PerPixelDofNode::create(make_cam2_5d(50.0f, 1.4f,  8.0f));
    auto node_d = PerPixelDofNode::create(make_cam2_5d(50.0f, 2.8f, 16.0f));
    auto shaped_camera = make_cam2_5d(50.0f, 2.8f, 8.0f);
    shaped_camera.dof.highlight_gain = 1.0f;
    auto node_e = PerPixelDofNode::create(shaped_camera);

    auto k_a = node_a->cache_key(ctx_a);
    auto k_b = node_b->cache_key(ctx_a);
    auto k_c = node_c->cache_key(ctx_a);
    auto k_d = node_d->cache_key(ctx_a);
    auto k_e = node_e->cache_key(ctx_a);

    CHECK(k_a.scope == "per_pixel_dof");
    CHECK(k_a.params_hash != k_b.params_hash);
    CHECK(k_a.params_hash != k_c.params_hash);
    CHECK(k_a.params_hash != k_d.params_hash);
    CHECK(k_a.params_hash != k_e.params_hash);
}

TEST_CASE("PR2-Unit-DoF: predicted_bbox expands symmetrically by max_blur") {
    using chronon3d::graph::PerPixelDofNode;

    auto node = PerPixelDofNode::create(make_cam2_5d(50.0f, 2.8f, 12.0f));
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
    d.focus_distance = 100.0f;
    d.max_blur = 16.0f;
    LensModel lens;
    lens.focal_length = 50.0f;
    lens.f_stop = 2.8f;
    lens.sensor_width = 36.0f;
    const float r = compute_dof_blur_radius(d, lens, /*layer_world_z=*/500.0f, 1920.0f);
    CHECK(r == doctest::Approx(0.0f));
}
