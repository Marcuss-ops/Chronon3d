// ==============================================================================
// tests/render_graph/nodes/test_mask_node_rg_integration.cpp
//
// PR2 — MaskNode render-graph integration tests (3 tests).
//
// Drives the full SoftwareRenderer pipeline with masks and verifies
// pixel-level outcomes:
//   1. Rect mask_rect clips a circle to a square (alpha multiplied).
//   2. Inverted mask_rect (inverted=true) renders alpha inversely.
//   3. Modular-coordinates determinism (same scene → byte equal hash).
//
// Uses LayerBuilder.mask_rect(RectMaskParams{...}) — the actual API.
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
#include <cstdint>
#include <memory>
using namespace chronon3d;
namespace ctt = chronon3d::test;

TEST_CASE("PR2-RG-Mask: rectangular mask_rect clips a circle into a square") {
    auto r = ctt::make_renderer(false);
    auto comp = composition({.width = 256, .height = 256, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.10f, 0.10f, 1.0f});
            });
            s.layer("masked", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.mask_rect(RectMaskParams{
                    .size = {120.0f, 120.0f},
                    .pos  = Vec3{0.0f, 0.0f, 0.0f},
                    .inverted = false,
                });
                l.circle("c", {
                    .radius = 80.0f,
                    .color = {1.0f, 0.7f, 0.2f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
    auto fb = r.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    const Color outside_clip = fb->get_pixel(20, 20);
    CHECK(outside_clip.a < 0.05f);

    const Color center = fb->get_pixel(128, 128);
    CHECK(center.a > 0.85f);
}

TEST_CASE("PR2-RG-Mask: inverted mask_rect zeroes interior alpha") {
    auto r = ctt::make_renderer(false);
    auto comp = composition({.width = 256, .height = 256, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.10f, 0.10f, 1.0f});
            });
            s.layer("inv", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.mask_rect(RectMaskParams{
                    .size = {120.0f, 120.0f},
                    .pos  = Vec3{0.0f, 0.0f, 0.0f},
                    .inverted = true,
                });
                l.circle("c", {
                    .radius = 80.0f,
                    .color = {1.0f, 0.7f, 0.2f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
    auto fb = r.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    const Color centre = fb->get_pixel(128, 128);
    CHECK(centre.a < 0.05f);

    const Color outside = fb->get_pixel(20, 20);
    CHECK(outside.a > 0.50f);
}

TEST_CASE("PR2-RG-Mask: render is deterministic across two calls") {
    auto r = ctt::make_renderer();
    auto comp = composition({.width = 256, .height = 256, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.10f, 0.10f, 1.0f});
            });
            s.layer("mod", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.mask_rect(RectMaskParams{
                    .size = {120.0f, 120.0f},
                    .pos  = Vec3{0.0f, 0.0f, 0.0f},
                });
                l.circle("c", {
                    .radius = 80.0f,
                    .color = {1.0f, 0.7f, 0.2f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
    auto fb1 = r.render_frame(comp, 0);
    auto fb2 = r.render_frame(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(ctt::framebuffer_alpha_hash(*fb1) == ctt::framebuffer_alpha_hash(*fb2));
}
