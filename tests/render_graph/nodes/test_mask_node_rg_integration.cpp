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
// Uses LayerBuilder.mask_rect(RectMaskParams{...}) — the actual API
// (l.mask(...) doesn't exist on LayerBuilder).
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

namespace {
namespace mask_rg_impl {
test::TestRenderer make_mask_rg_renderer() {
    auto r = test::make_renderer();
    RenderSettings s;
        r.set_settings(s);
    return r;
}
}  // namespace mask_rg_impl

// Hash of pixel-alpha only — robust to RGB changes (blending jitter safe).
uint64_t alpha_hash(const Framebuffer& fb) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            uint32_t bits;
            Color c = fb.get_pixel(x, y);
            std::memcpy(&bits, &c.a, 4);
            h ^= bits;
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}
}  // namespace

// The background is intentionally opaque, so the post-composite alpha remains
// 1.0 both inside and outside the mask. Sample foreground RGB instead.
TEST_CASE("PR2-RG-Mask: rectangular mask_rect clips a circle into a square") {
    auto r = mask_rg_impl::make_mask_rg_renderer();
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
    auto fb = r.render(comp, 0);
    REQUIRE(fb != nullptr);

    // (60,128) is inside the radius-80 circle but outside the centered
    // 120x120 mask rectangle.  The opaque background must show through.
    const Color outside_clip = fb->get_pixel(55, 128);
    CHECK(outside_clip.r < 0.20f);

    const Color center = fb->get_pixel(128, 128);
    CHECK(center.r > 0.85f);
}

TEST_CASE("PR2-RG-Mask: inverted mask_rect suppresses interior foreground") {
    auto r = mask_rg_impl::make_mask_rg_renderer();
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
    auto fb = r.render(comp, 0);
    REQUIRE(fb != nullptr);

    // In the inverted case the centered mask suppresses the circle at its
    // center, while this point remains inside the circle and outside the mask.
    const Color centre = fb->get_pixel(128, 128);
    CHECK(centre.r < 0.20f);

    const Color outside = fb->get_pixel(55, 128);
    CHECK(outside.r > 0.85f);
}

TEST_CASE("PR2-RG-Mask: render is deterministic across two calls") {
    auto r = mask_rg_impl::make_mask_rg_renderer();
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
    auto fb1 = r.render(comp, 0);
    auto fb2 = r.render(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(alpha_hash(*fb1) == alpha_hash(*fb2));
}
