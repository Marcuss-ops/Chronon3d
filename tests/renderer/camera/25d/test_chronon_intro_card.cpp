#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <cmath>
#include <string>

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

// Renders ChrononIntroCard at frame f (half-res for speed)
std::shared_ptr<Framebuffer> render_intro(Frame f) {
    auto renderer = make_renderer();
    Composition comp({.name="ChrononIntroCardTest", .width=640, .height=360, .duration=180},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            const float t = ctx.duration > 1
                ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1) : 0.0f;

            auto ease = [](float a, float b, float ta, float tb, float tt) -> float {
                const float lt = std::clamp((tt - ta) / std::max(tb - ta, 1e-6f), 0.0f, 1.0f);
                const float st = lt * lt * (3.0f - 2.0f * lt);
                return a + (b - a) * st;
            };

            const float cam_x = ease(-28.0f, 28.0f, 0.0f, 1.0f, t);
            const float cam_z = ease(-1080.0f, -960.0f, 0.0f, 0.70f, t);

            s.camera().set({
                .enabled  = true,
                .position = {cam_x, 0.0f, cam_z},
                .zoom     = 1000.0f,
                .dof      = {.enabled=true, .focus_z=0.0f, .aperture=0.013f, .max_blur=11.0f}
            });

            s.layer("bg", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 600.0f});
                l.rect("base", {.size={2800.0f,1600.0f}, .color={0.04f,0.04f,0.09f,1.0f}, .pos={0,0,0}});
                l.circle("orb_l", {.radius=280.0f, .color={0.07f,0.05f,0.32f,0.55f}, .pos={-440.0f,-70.0f,0.0f}}).blur(75.0f);
            });

            s.layer("card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("panel", {.size={660.0f,380.0f}, .radius=18.0f,
                                          .color={0.045f,0.045f,0.11f,0.90f}, .pos={0,0,0}});
                l.rect("accent_t", {.size={560.0f,3.0f}, .color={0.42f,0.64f,1.00f,1.0f},
                                    .pos={0.0f,-171.0f,0.0f}});
            });

            s.layer("decoration", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f}).opacity(1.0f);
                l.rect("stripe", {.size={240.0f,2.0f}, .color={0.42f,0.64f,1.00f,0.6f},
                                  .pos={0.0f, 0.0f, 0.0f}});
            });

            return s.build();
        });
    return renderer.render_frame(comp, f);
}

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

} // namespace

TEST_CASE("ChrononIntroCard: renders without crash at key frames") {
    if (!std::filesystem::exists("assets/fonts/Inter-Bold.ttf")) return;
    auto fb0   = render_intro(0);
    auto fb45  = render_intro(45);
    auto fb90  = render_intro(90);
    auto fb179 = render_intro(179);

    REQUIRE(fb0   != nullptr);
    REQUIRE(fb45  != nullptr);
    REQUIRE(fb90  != nullptr);
    REQUIRE(fb179 != nullptr);

    save_debug(*fb0,   "output/debug/demos/intro_f000.png");
    save_debug(*fb45,  "output/debug/demos/intro_f045.png");
    save_debug(*fb90,  "output/debug/demos/intro_f090.png");
    save_debug(*fb179, "output/debug/demos/intro_f179.png");
}

TEST_CASE("ChrononIntroCard: card center is brighter than dark corners at frame 0") {
    if (!std::filesystem::exists("assets/fonts/Inter-Bold.ttf")) return;
    auto fb = render_intro(0);
    REQUIRE(fb != nullptr);

    const float center = region_brightness(*fb, 200, 120, 440, 240);
    const float corner = region_brightness(*fb, 0, 0, 60, 45);
    CHECK(center > corner);
}

TEST_CASE("ChrononIntroCard: center band is visible by frame 90") {
    if (!std::filesystem::exists("assets/fonts/Inter-Bold.ttf")) return;
    auto fb = render_intro(90);
    REQUIRE(fb != nullptr);

    const float center = region_brightness(*fb, 180, 100, 460, 220);
    CHECK(center > 0.01f);  // not pure black — card and/or decoration visible
}
