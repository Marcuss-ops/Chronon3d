// ==============================================================================
// tests/render_graph/nodes/test_per_pixel_dof_node_rg_integration.cpp
//
// PR2 — PerPixelDofNode render-graph integration tests.
//
// Drives the full SoftwareRenderer pipeline and verifies pixel-level and
// depth-provenance outcomes. RenderSettings does NOT expose a `dof` field;
// DoF state lives on the Camera2_5DRuntime supplied to PerPixelDofNode.
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/camera/camera_common_types.hpp>
#include <tests/helpers/test_utils.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
using namespace chronon3d;
namespace ctt = chronon3d::test;

Composition make_dof_scene(bool far_bar) {
    return composition({.width = 256, .height = 256, .duration = 1},
        [far_bar](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true).dof(DepthOfFieldSettings{
                .enabled = true,
                .focus_z = 0.0f,
                .aperture = 0.05f,
                .max_blur = 24.0f
            });
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.05f, 0.07f, 0.10f, 1.0f});
            });
            s.layer("lines", [far_bar](LayerBuilder& l) {
                l.position({0.0f, 0.0f, far_bar ? -800.0f : 0.0f});
                for (int i = 0; i < 8; ++i) {
                    l.rect("r" + std::to_string(i), {
                        .size = {200.0f, 1.5f},
                        .color = {1.0f, 1.0f, 1.0f, 1.0f},
                        .pos = {0.0f, -50.0f + static_cast<float>(i) * 14.0f, 0.0f}
                    });
                }
            });
            return s.build();
        });
}

Composition make_sparse_dof_scene() {
    return composition({.width = 128, .height = 128, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true).dof(DepthOfFieldSettings{
                .enabled = true,
                .focus_z = 0.0f,
                .aperture = 0.05f,
                .max_blur = 24.0f
            });
            s.layer("sparse", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, -800.0f});
                l.rect("box", {
                    .size = {24.0f, 24.0f},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
}

TEST_CASE("PR2-RG-DoF: smoke render produces expected dimensions") {
    auto r = ctt::make_renderer();
    auto fb = r.render(make_dof_scene(false), 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 256);
    CHECK(fb->height() == 256);
}

TEST_CASE("PR2-RG-DoF: two consecutive renders are byte-equal (determinism)") {
    auto r = ctt::make_renderer();
    auto fb1 = r.render(make_dof_scene(false), 0);
    auto fb2 = r.render(make_dof_scene(false), 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(ctt::framebuffer_hash(*fb1) == ctt::framebuffer_hash(*fb2));
}

TEST_CASE("PR2-RG-DoF: per-element z-range variation produces differing hashes") {
    auto r = ctt::make_renderer();
    auto fb_near = r.render(make_dof_scene(false), 0);
    auto fb_far  = r.render(make_dof_scene(true),  0);
    REQUIRE(fb_near != nullptr);
    REQUIRE(fb_far  != nullptr);
    CHECK(ctt::framebuffer_hash(*fb_near) != ctt::framebuffer_hash(*fb_far));
}

TEST_CASE("PR2-RG-DoF: untouched pixels are not classified as blur sources") {
    constexpr uint64_t kPixels = 128ULL * 128ULL;

    auto r = ctt::make_renderer();
    r.reset_counters();
    auto fb = r.render(make_sparse_dof_scene(), 0);
    REQUIRE(fb != nullptr);

    const uint64_t blur_sources = r.counters()->dof_blur_source_pixels.load(
        std::memory_order_relaxed);

    // Regression for the depth-provenance bug where OutputPass initialized
    // every untouched pixel to z=0.  Zero is a valid world depth, so the DOF
    // analyzer classified essentially the complete canvas as defocused.  A
    // sparse scene must leave the vast majority of the depth plane unset.
    CHECK(blur_sources > 0);
    CHECK(blur_sources < kPixels / 4);
}
