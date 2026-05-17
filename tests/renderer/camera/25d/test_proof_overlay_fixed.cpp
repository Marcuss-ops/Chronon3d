#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

SoftwareRenderer make_renderer() {
    SoftwareRenderer r;
    RenderSettings s;
    s.use_modular_graph = true;
    r.set_settings(s);
    return r;
}

// Pan scene with a 2D (no enable_3d) yellow circle and a 3D blue card.
std::unique_ptr<Framebuffer> render_overlay_frame(float pan_t) {
    auto renderer = make_renderer();
    Composition comp({.name = "OverlayTest", .width = 640, .height = 480, .duration = 1},
        [pan_t](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set(camera_motion::pan(pan_t, {
                .from_x = -120.0f,
                .to_x   =  120.0f,
                .z      = -1000.0f,
                .zoom   = 1000.0f
            }));
            s.rect("bg", {
                .size  = {640.0f, 480.0f},
                .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
                .pos   = {320.0f, 240.0f, 0.0f}
            });
            // 3D card — moves with camera pan
            s.layer("scene_card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("card", {.size = {200.0f, 120.0f}, .color = Color{0.1f, 0.2f, 1.0f, 1.0f}, .pos = {0, 0, 0}});
            });
            // 2D HUD — no enable_3d, fixed in screen space
            s.layer("hud", [](LayerBuilder& l) {
                l.position({-260.0f, -180.0f, 0.0f}); // top-left quadrant
                l.circle("dot", {.radius = 20.0f, .color = Color{1.0f, 0.9f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

} // namespace

TEST_CASE("Proof — OverlayFixed: 2D HUD layer stays fixed while camera pans") {
    const Color yellow_sel = Color{1.0f, 0.9f, 0.1f, 1.0f};

    auto fb_start = render_overlay_frame(0.0f);
    auto fb_end   = render_overlay_frame(1.0f);

    REQUIRE(fb_start != nullptr);
    REQUIRE(fb_end   != nullptr);

    save_debug(*fb_start, "output/debug/proofs/overlay_fixed/pan_start.png");
    save_debug(*fb_end,   "output/debug/proofs/overlay_fixed/pan_end.png");

    const float hud_x0 = centroid_x(*fb_start, yellow_sel);
    const float hud_x1 = centroid_x(*fb_end,   yellow_sel);

    CHECK(hud_x0 > 0.0f); // HUD visible at start
    CHECK(hud_x1 > 0.0f); // HUD visible at end

    // HUD X must not shift — 2D layer ignores camera
    CHECK(std::abs(hud_x1 - hud_x0) < 3.0f);
}

TEST_CASE("Proof — OverlayFixed: 3D card DOES move during camera pan") {
    const Color blue_sel = Color{0.1f, 0.2f, 1.0f, 1.0f};

    auto fb_start = render_overlay_frame(0.0f);
    auto fb_end   = render_overlay_frame(1.0f);

    REQUIRE(fb_start != nullptr);
    REQUIRE(fb_end   != nullptr);

    const float card_x0 = centroid_x(*fb_start, blue_sel);
    const float card_x1 = centroid_x(*fb_end,   blue_sel);

    // 3D card must actually shift — confirms camera is working
    if (card_x0 > 0.0f && card_x1 > 0.0f) {
        CHECK(std::abs(card_x1 - card_x0) > 5.0f);
    }
}
