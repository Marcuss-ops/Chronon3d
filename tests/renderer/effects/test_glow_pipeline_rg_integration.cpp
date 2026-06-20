// ==============================================================================
// tests/renderer/effects/test_glow_pipeline_rg_integration.cpp
//
// PR2 — GlowPipeline render-graph integration tests (3 tests).
//
// Drives the full SoftwareRenderer pipeline with various glow parameters
// and verifies pixel-level outcomes:
//   1. Layer-mode glow: bright source on dark bg produces halo expansion.
//   2. Bloom triggered via l.bloom(threshold, radius, intensity).
//      Note: l.bloom(BloomParams{...}) is not the API; LayerBuilder takes
//      three floats not a struct.
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

#include <cmath>
#include <cstdint>
#include <memory>
using namespace chronon3d;

namespace {
SoftwareRenderer make_renderer() {
    SoftwareRenderer r;
    RenderSettings s;
    s.use_modular_graph = true;
    r.set_settings(s);
    return r;
}

float luma(const Color& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

uint64_t fb_hash(const Framebuffer& fb) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const auto c = fb.get_pixel(x, y);
            auto fold = [&](float v) {
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                h ^= bits;
                h *= 0x100000001b3ULL;
            };
            fold(c.r); fold(c.g); fold(c.b); fold(c.a);
        }
    }
    return h;
}

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
                // API: l.bloom(threshold, radius, intensity) — three floats.
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
}  // namespace

TEST_CASE("PR2-RG-Glow: layer-mode glow creates a halo around bright source") {
    auto r = make_renderer();
    auto fb = r.render_frame(make_glow_layer_scene(), 0);
    REQUIRE(fb != nullptr);

    const Color far_bg = fb->get_pixel(8, 8);
    const float far_luma = luma(far_bg);

    const Color halo = fb->get_pixel(128 + 30, 128);
    const float halo_luma = luma(halo);

    CHECK(halo_luma > far_luma + 0.005f);
}

TEST_CASE("PR2-RG-Glow: bloom threshold masks dim sources") {
    auto r = make_renderer();
    auto fb = r.render_frame(make_bloom_threshold_scene(), 0);
    REQUIRE(fb != nullptr);

    const Color hal_dim    = fb->get_pixel(128 - 40 + 22, 128);
    const Color hal_bright = fb->get_pixel(128 + 40 + 22, 128);

    CHECK(luma(hal_bright) > luma(hal_dim) * 2.0f);
}

TEST_CASE("PR2-RG-Glow: consecutive renders are deterministic") {
    auto r = make_renderer();
    auto fb1 = r.render_frame(make_glow_layer_scene(), 0);
    auto fb2 = r.render_frame(make_glow_layer_scene(), 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(fb_hash(*fb1) == fb_hash(*fb2));
}
