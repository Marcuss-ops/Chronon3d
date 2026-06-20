// ==============================================================================
// tests/renderer/effects/test_glow_pipeline_rg_integration.cpp
//
// PR2 — GlowPipeline render-graph integration tests (3 tests).
//
// Drives the full SoftwareRenderer pipeline with various glow parameters
// and verifies pixel-level outcomes:
//   1. Layer-mode glow: bright source on dark bg produces halo expansion.
//   2. Bloom triggered via l.bloom(threshold, radius, intensity).
//   3. Two consecutive renders are byte-equal (determinism).
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/presets/glow_presets.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
using namespace chronon3d;
namespace ctt = chronon3d::test;

Composition make_glow_layer_scene() {
    return composition({.width = 256, .height = 256, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.005f, 0.008f, 0.020f, 1.0f});
            });
            s.layer("glow_circle", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                GlowParams g = GlowPresets::neon_blue(28.0f);
                g.intensity = 0.9f;
                g.additive = false;
                l.glow(g);
                l.circle("core", {
                    .radius = 18.0f,
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
}

Composition make_bloom_threshold_scene() {
    return composition({.width = 256, .height = 256, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
            });
            s.layer("thr", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.bloom(/*threshold=*/0.80f, /*radius=*/18.0f, /*intensity=*/1.0f);
                l.rect("dim", {
                    .size = {28.0f, 28.0f},
                    .color = {0.40f, 0.40f, 0.40f, 1.0f},
                    .pos = {-40.0f, 0.0f, 0.0f}
                });
                l.rect("bright", {
                    .size = {28.0f, 28.0f},
                    .color = {2.0f, 2.0f, 2.0f, 1.0f},
                    .pos = {+40.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
}

TEST_CASE("PR2-RG-Glow: layer-mode glow creates a halo around bright source") {
    auto r = ctt::make_renderer();
    auto fb = r.render_frame(make_glow_layer_scene(), 0);
    REQUIRE(fb != nullptr);

    const Color far_bg = fb->get_pixel(8, 8);
    const float far_luma = ctt::luma(far_bg);

    const Color halo = fb->get_pixel(128 + 30, 128);
    const float halo_luma = ctt::luma(halo);

    CHECK(halo_luma > far_luma + 0.005f);
}

TEST_CASE("PR2-RG-Glow: bloom threshold masks dim sources") {
    auto r = ctt::make_renderer();
    auto fb = r.render_frame(make_bloom_threshold_scene(), 0);
    REQUIRE(fb != nullptr);

    const Color hal_dim    = fb->get_pixel(128 - 40 + 22, 128);
    const Color hal_bright = fb->get_pixel(128 + 40 + 22, 128);

    CHECK(ctt::luma(hal_bright) > ctt::luma(hal_dim) * 2.0f);
}

TEST_CASE("PR2-RG-Glow: consecutive renders are deterministic") {
    auto r = ctt::make_renderer();
    auto fb1 = r.render_frame(make_glow_layer_scene(), 0);
    auto fb2 = r.render_frame(make_glow_layer_scene(), 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(ctt::framebuffer_hash(*fb1) == ctt::framebuffer_hash(*fb2));
}
