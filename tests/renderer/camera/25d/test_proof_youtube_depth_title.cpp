#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <filesystem>
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

// Renders DepthTitle at frame f
std::unique_ptr<Framebuffer> render_depth_title(Frame f) {
    auto renderer = make_renderer();
    Composition comp({.name="YTDepthTitle", .width=640, .height=360, .duration=90},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const float t  = ctx.duration > 1
                ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1) : 0.0f;
            const float st = camera_motion::smoothstep(t);
            s.camera().set({
                .enabled  = true,
                .position = {camera_motion::lerp(-40.0f, 40.0f, st), 0.0f,
                             camera_motion::lerp(-1000.0f, -850.0f, st)},
                .zoom = 1000.0f,
                .dof  = {.enabled=true, .focus_z=0.0f, .aperture=0.022f, .max_blur=20.0f}
            });

            // Background (z=700)
            s.layer("bg", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 700.0f});
                l.rect("base", {.size={1280.0f,720.0f}, .color={0.04f,0.045f,0.08f,1.0f}, .pos={0,0,0}});
                l.circle("b1", {.radius=300.0f, .color={0.05f,0.12f,0.45f,0.55f}, .pos={-350.0f,-50.0f,0.0f}}).blur(50.0f);
                l.circle("b2", {.radius=200.0f, .color={0.25f,0.06f,0.35f,0.4f},  .pos={300.0f,100.0f,0.0f}}).blur(35.0f);
            });

            // Overlay (z=400)
            s.layer("ov", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 400.0f});
                l.rect("v", {.size={1200.0f,720.0f}, .color={0.0f,0.0f,0.0f,0.50f}, .pos={0,0,0}});
            });

            // Subject (z=0)
            s.layer("sub", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, -20.0f, 0.0f});
                l.rounded_rect("card", {.size={300.0f,380.0f}, .radius=10.0f,
                                         .color={0.62f,0.55f,0.48f,1.0f}, .pos={0,0,0}});
            });

            // Badge 2D
            s.rect("badge", {.size={80.0f,24.0f}, .color={0.90f,0.15f,0.12f,1.0f}, .pos={580.0f,24.0f,0.0f}});
            return s.build();
        });
    return renderer.render_frame(comp, f);
}

// Measure average brightness of a region
float region_brightness(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    double sum = 0; int cnt = 0;
    for (int y = y0; y < y1 && y < fb.height(); ++y)
        for (int x = x0; x < x1 && x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            sum += (c.r + c.g + c.b) / 3.0;
            ++cnt;
        }
    return cnt > 0 ? static_cast<float>(sum / cnt) : 0.0f;
}

// Detect red badge pixels
int count_red_badge(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1 && y < fb.height(); ++y)
        for (int x = x0; x < x1 && x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (c.r > 0.7f && c.g < 0.35f && c.b < 0.35f) ++n;
        }
    return n;
}

} // namespace

TEST_CASE("Proof YT — DepthTitle: scene renders without crashing") {
    auto fb0 = render_depth_title(0);
    auto fb45 = render_depth_title(45);
    auto fb89 = render_depth_title(89);

    REQUIRE(fb0  != nullptr);
    REQUIRE(fb45 != nullptr);
    REQUIRE(fb89 != nullptr);

    save_debug(*fb0,  "output/debug/proofs/youtube/depth_title_f000.png");
    save_debug(*fb45, "output/debug/proofs/youtube/depth_title_f045.png");
    save_debug(*fb89, "output/debug/proofs/youtube/depth_title_f089.png");
}

TEST_CASE("Proof YT — DepthTitle: scene has visible subject (brighter than pure background)") {
    auto fb = render_depth_title(0);
    REQUIRE(fb != nullptr);

    // Center (subject) should be noticeably brighter than background corner
    const float center_bright  = region_brightness(*fb, 240, 120, 400, 240);
    const float corner_bright  = region_brightness(*fb, 0, 0, 80, 60);

    CHECK(center_bright > corner_bright);
}

TEST_CASE("Proof YT — DepthTitle: 2D badge is stable across pan") {
    auto fb0 = render_depth_title(0);
    auto fb89 = render_depth_title(89);
    REQUIRE(fb0  != nullptr);
    REQUIRE(fb89 != nullptr);

    // Badge at top-right on 640x360 canvas: ~(540,10)-(620,34)
    const int badge_f0  = count_red_badge(*fb0,  530, 8, 620, 38);
    const int badge_f89 = count_red_badge(*fb89, 530, 8, 620, 38);

    CHECK(badge_f0  > 0);  // badge visible frame 0
    CHECK(badge_f89 > 0);  // badge visible frame 89
    // Badge pixel count stable — not moved
    CHECK(std::abs(badge_f89 - badge_f0) < badge_f0 / 4 + 10);
}
