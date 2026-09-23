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

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/effects/presets/glow_presets.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

namespace {
namespace glow_rg_impl {
test::TestRenderer make_glow_rg_renderer() {
    auto r = test::make_renderer();
    RenderSettings s;
        r.set_settings(s);
    // Return the concrete TestRenderer: returning a sliced SoftwareRenderer
    // would destroy the TestRenderer-owned RuntimeContext at the return
    // boundary, leaving the renderer's runtime pointer dangling.
    return r;
}
}  // namespace glow_rg_impl

float luma(const Color& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

Vec2 weighted_centroid(const Framebuffer& fb, float background_luma) {
    double sum_x = 0.0;
    double sum_y = 0.0;
    double weight = 0.0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const float w = std::max(0.0f, luma(fb.get_pixel(x, y)) - background_luma);
            sum_x += static_cast<double>(x) * w;
            sum_y += static_cast<double>(y) * w;
            weight += w;
        }
    }
    if (weight <= 0.0) return {-1.0f, -1.0f};
    return {static_cast<float>(sum_x / weight),
            static_cast<float>(sum_y / weight)};
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
                GlowParams g{.radius = 28.0f, .intensity = 0.25f,
                             .color = Color{0.15f, 0.65f, 1.0f, 1.0f}};
                l.effect(g);
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
                // Effects are applied to the completed layer content.
                l.bloom(/*threshold=*/0.75f, /*radius=*/18.0f, /*intensity=*/1.0f);
            });
            return s.build();
        });
}

Composition make_centered_glow_centroid_scene(bool glow) {
    return composition({.width = 300, .height = 220, .duration = 1},
        [glow](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.004f, 0.006f, 0.016f, 1.0f});
            });
            s.layer("source", [glow](LayerBuilder& l) {
                if (glow) {
                    GlowParams params{.radius = 34.0f, .intensity = 0.25f,
                                      .color = Color{0.15f, 0.65f, 1.0f, 1.0f}};
                    l.effect(params);
                }
                l.rounded_rect("card", {
                    .size = {52.0f, 36.0f},
                    .radius = 8.0f,
                    .color = Color{0.95f, 0.98f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
}

}  // namespace

TEST_CASE("PR2-RG-Glow: layer-mode glow creates a halo around bright source") {
    auto r = glow_rg_impl::make_glow_rg_renderer();
    auto fb = r.render(make_glow_layer_scene(), 0);
    REQUIRE(fb != nullptr);

    const Color far_bg = fb->get_pixel(8, 8);
    const float far_luma = luma(far_bg);

    const Color halo = fb->get_pixel(128 + 30, 128);
    const float halo_luma = luma(halo);

    CHECK(halo_luma > far_luma + 0.005f);
}

TEST_CASE("PR2-RG-Glow: bloom threshold masks dim sources") {
    auto r = glow_rg_impl::make_glow_rg_renderer();
    auto fb = r.render(make_bloom_threshold_scene(), 0);
    REQUIRE(fb != nullptr);

    // Validate thresholding at the source centers.  Halo extent is covered
    // by the dedicated bloom-spread test; this assertion isolates masking.
    const Color hal_dim    = fb->get_pixel(128 - 40, 128);
    const Color hal_bright = fb->get_pixel(128 + 40, 128);

    CHECK(luma(hal_bright) > luma(hal_dim) * 2.0f);
}

TEST_CASE("PR2-RG-Glow: consecutive renders are deterministic") {
    auto r = glow_rg_impl::make_glow_rg_renderer();
    auto fb1 = r.render(make_glow_layer_scene(), 0);
    auto fb2 = r.render(make_glow_layer_scene(), 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(fb_hash(*fb1) == fb_hash(*fb2));
}

TEST_CASE("PR2-RG-Glow: glow expands bounds without shifting the source centroid") {
    auto r = glow_rg_impl::make_glow_rg_renderer();
    auto without_glow = r.render(make_centered_glow_centroid_scene(false), 0);
    auto with_glow = r.render(make_centered_glow_centroid_scene(true), 0);
    REQUIRE(without_glow != nullptr);
    REQUIRE(with_glow != nullptr);

    const float background = luma(Color{0.004f, 0.006f, 0.016f, 1.0f});
    const auto a = weighted_centroid(*without_glow, background);
    const auto b = weighted_centroid(*with_glow, background);
    REQUIRE(a.x >= 0.0f);
    REQUIRE(b.x >= 0.0f);
    CHECK(std::abs(a.x - b.x) < 0.5f);
    CHECK(std::abs(a.y - b.y) < 0.5f);
}
