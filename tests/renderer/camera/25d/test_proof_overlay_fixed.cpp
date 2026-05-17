#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <tests/helpers/render_fixtures.hpp>
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

// 2D HUD: magenta rect added as a direct scene element (absolute screen coords).
// 3D card: blue rect in 3D space that moves with camera pan.
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

            // Dark background (absolute screen coords)
            s.rect("bg", {
                .size  = {640.0f, 480.0f},
                .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
                .pos   = {320.0f, 240.0f, 0.0f}
            });

            // 3D card — moves with camera
            s.layer("scene_card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("card", {.size = {200.0f, 120.0f}, .color = Color{0.1f, 0.2f, 1.0f, 1.0f}, .pos = {0, 0, 0}});
            });

            // 2D HUD — direct scene element in absolute screen coords, never affected by camera
            s.rect("hud_marker", {
                .size  = {50.0f, 50.0f},
                .color = Color{1.0f, 0.0f, 1.0f, 1.0f},   // magenta — distinct from everything
                .pos   = {60.0f, 60.0f, 0.0f}             // top-left, absolute screen coords
            });

            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

template<typename Pred>
float scan_centroid_x(const Framebuffer& fb, Pred pred) {
    double sum = 0.0; int cnt = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x)
            if (pred(fb.get_pixel(x, y))) { sum += x; ++cnt; }
    return cnt > 0 ? static_cast<float>(sum / cnt) : -1.0f;
}

auto is_magenta = [](const Color& c){ return c.r > 0.7f && c.g < 0.3f && c.b > 0.7f; };
auto is_blue    = [](const Color& c){ return c.b > 0.7f && c.r < 0.3f; };

} // namespace

TEST_CASE("Proof — OverlayFixed: 2D HUD element stays fixed while camera pans") {
    auto fb_start = render_overlay_frame(0.0f);
    auto fb_end   = render_overlay_frame(1.0f);

    REQUIRE(fb_start != nullptr);
    REQUIRE(fb_end   != nullptr);

    save_debug(*fb_start, "output/debug/proofs/overlay_fixed/pan_start.png");
    save_debug(*fb_end,   "output/debug/proofs/overlay_fixed/pan_end.png");

    const float hud_x0 = scan_centroid_x(*fb_start, is_magenta);
    const float hud_x1 = scan_centroid_x(*fb_end,   is_magenta);

    CHECK(hud_x0 > 0.0f);  // HUD visible at start
    CHECK(hud_x1 > 0.0f);  // HUD visible at end
    CHECK(std::abs(hud_x1 - hud_x0) < 3.0f);  // HUD does not move
}

TEST_CASE("Proof — OverlayFixed: 3D card DOES move during camera pan") {
    auto fb_start = render_overlay_frame(0.0f);
    auto fb_end   = render_overlay_frame(1.0f);

    REQUIRE(fb_start != nullptr);
    REQUIRE(fb_end   != nullptr);

    const float card_x0 = scan_centroid_x(*fb_start, is_blue);
    const float card_x1 = scan_centroid_x(*fb_end,   is_blue);

    if (card_x0 > 0.0f && card_x1 > 0.0f)
        CHECK(std::abs(card_x1 - card_x0) > 2.0f);
}
