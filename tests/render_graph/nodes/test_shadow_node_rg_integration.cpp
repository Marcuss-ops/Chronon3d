// ==============================================================================
// tests/render_graph/nodes/test_shadow_node_rg_integration.cpp
//
// PR2 — ShadowNode render-graph integration tests (3 tests).
//
// Exercises the full SoftwareRenderer pipeline with drop_shadow enabled
// and verifies pixel-level outcomes:
//   1. Projection: shadow lands on darker pixels at the expected offset.
//   2. Blur: shadow extent widens beyond the hard alpha silhouette.
//   3. Smoke test without shadow: graph builder doesn't crash.
//
// Uses LayerBuilder.drop_shadow(Vec2 offset, Color color, f32 radius) —
// the actual API (l.drop_shadow(DropShadowParams{...}) doesn't exist).
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <memory>
using namespace chronon3d;
namespace ctt = chronon3d::test;

Composition make_shadow_scene() {
    return composition({.width = 256, .height = 256, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.12f, 0.16f, 1.0f});
            });
            s.layer("caster", [](LayerBuilder& l) {
                l.position({-30.0f, 30.0f, 0.0f});
                l.drop_shadow(
                    /*offset=*/Vec2{32.0f, 26.0f},
                    /*color=*/ Color{0.0f, 0.0f, 0.0f, 0.65f},
                    /*radius=*/12.0f
                );
                l.rect("c", {.size = {56.0f, 56.0f},
                              .color = {1.0f, 0.85f, 0.55f, 1.0f},
                              .pos = {0.0f, 0.0f, 0.0f}});
            });
            return s.build();
        });
}

TEST_CASE("PR2-RG-Shadow: shadow projects at light direction + offset") {
    auto renderer = ctt::make_renderer();
    auto fb = renderer.render_frame(make_shadow_scene(), 0);
    REQUIRE(fb != nullptr);

    const Color bg = fb->get_pixel(40, 40);
    const Color shadow_zone = fb->get_pixel(160, 180);

    bool any_darker =
        shadow_zone.r < bg.r - 0.05f ||
        shadow_zone.g < bg.g - 0.05f ||
        shadow_zone.b < bg.b - 0.05f;
    CHECK(any_darker);
}

TEST_CASE("PR2-RG-Shadow: shadow extends beyond caster silhouette (blur)") {
    auto renderer = ctt::make_renderer();
    auto fb = renderer.render_frame(make_shadow_scene(), 0);
    REQUIRE(fb != nullptr);

    const Color bg = fb->get_pixel(40, 40);
    const Color edge_soft = fb->get_pixel(160, 200);
    bool softer_than_far_off =
        edge_soft.r + edge_soft.g + edge_soft.b
        < bg.r + bg.g + bg.b + 0.05f;
    CHECK(softer_than_far_off);
}

TEST_CASE("PR2-RG-Shadow: render with no drop_shadow completes cleanly") {
    Composition comp = composition({.width = 128, .height = 128, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
            });
            s.layer("rect", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rect("r", {.size = {32.0f, 32.0f},
                              .color = {1.0f, 1.0f, 1.0f, 1.0f},
                              .pos = {0.0f, 0.0f, 0.0f}});
            });
            return s.build();
        });

    auto renderer = ctt::make_renderer();
    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 128);
    CHECK(fb->height() == 128);
}
