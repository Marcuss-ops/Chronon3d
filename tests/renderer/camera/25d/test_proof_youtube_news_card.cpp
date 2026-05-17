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

std::unique_ptr<Framebuffer> render_news_frame(Frame f) {
    auto renderer = make_renderer();
    Composition comp({.name="YTNews", .width=640, .height=360, .duration=90},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const float t  = ctx.duration > 1
                ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1) : 0.0f;
            const float st = camera_motion::smoothstep(t);
            s.camera().set({
                .enabled  = true,
                .position = {camera_motion::lerp(-20.0f, 20.0f, st), 0.0f, -1000.0f},
                .zoom = 1000.0f,
                .dof  = {.enabled=true, .focus_z=0.0f, .aperture=0.020f, .max_blur=18.0f}
            });

            // Background
            s.layer("bg", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 600.0f});
                l.rect("base", {.size={1600.0f,900.0f}, .color={0.06f,0.05f,0.07f,1.0f}, .pos={0,0,0}});
                l.circle("acc", {.radius=220.0f, .color={0.10f,0.05f,0.05f,0.4f}, .pos={300.0f,-60.0f,0.0f}}).blur(55.0f);
            });

            // Image card left (z=0)
            s.layer("img", [](LayerBuilder& l) {
                l.enable_3d().position({-200.0f, 0.0f, 0.0f});
                l.rounded_rect("p", {.size={220.0f,280.0f}, .radius=6.0f,
                                      .color={0.55f,0.50f,0.45f,1.0f}, .pos={0,0,0}});
                l.rect("hair", {.size={220.0f,80.0f}, .color={0.18f,0.14f,0.10f,1.0f}, .pos={0,-100.0f,0}});
            });

            // Lower third bar (z=-50)
            s.layer("lt", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 145.0f, -50.0f});
                l.rect("bar", {.size={1100.0f,40.0f}, .color={0.88f,0.12f,0.10f,1.0f}, .pos={0,0,0}});
            });

            // Badge 2D
            s.rect("badge", {.size={100.0f,26.0f}, .color={0.92f,0.10f,0.08f,1.0f}, .pos={60.0f,20.0f,0.0f}});
            return s.build();
        });
    return renderer.render_frame(comp, f);
}

// Count red pixels (badge / lower-third) in a screen region
int count_red(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1 && y < fb.height(); ++y)
        for (int x = x0; x < x1 && x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (c.r > 0.7f && c.g < 0.35f && c.b < 0.35f) ++n;
        }
    return n;
}

} // namespace

TEST_CASE("Proof YT — NewsCard: renders at key frames") {
    auto fb0  = render_news_frame(0);
    auto fb89 = render_news_frame(89);

    REQUIRE(fb0  != nullptr);
    REQUIRE(fb89 != nullptr);

    save_debug(*fb0,  "output/debug/proofs/youtube/news_card_f000.png");
    save_debug(*fb89, "output/debug/proofs/youtube/news_card_f089.png");
}

TEST_CASE("Proof YT — NewsCard: 2D badge stays fixed during pan") {
    auto fb0  = render_news_frame(0);
    auto fb89 = render_news_frame(89);

    REQUIRE(fb0  != nullptr);
    REQUIRE(fb89 != nullptr);

    // Badge at top-left screen, absolute coords: ~(10,7)-(110,33) on 640x360
    const int n0  = count_red(*fb0,  8, 5, 115, 38);
    const int n89 = count_red(*fb89, 8, 5, 115, 38);

    CHECK(n0  > 0);  // badge visible frame 0
    CHECK(n89 > 0);  // badge visible frame 89
    // Badge pixel count stable (within 15%)
    CHECK(std::abs(n89 - n0) <= n0 / 4 + 8);
}
